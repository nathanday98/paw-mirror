#include "render_graph.h"

#include <core/gfx.h>
#include <core/slice.inl>
#include <core/logger.h>
#include <core/math.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-enum-value"
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wunused-value"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#include <Windows.h>
#include <ShellScalingApi.h>
#include <d3d12/d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <d3d12shader.h>
#include <WinPixEventRuntime/pix3.h>
#include <new>
#include <string>
#include <algorithm>
#pragma clang diagnostic pop

#include "render_graph.h"
#include "gfx_dx12.h"

static constexpr D3D12_BARRIER_SYNC g_access_to_sync_map[int(Gfx::Access::Count)]{
	D3D12_BARRIER_SYNC_NONE,
	D3D12_BARRIER_SYNC_RENDER_TARGET,
	D3D12_BARRIER_SYNC_VERTEX_SHADING,
	D3D12_BARRIER_SYNC_PIXEL_SHADING,
	D3D12_BARRIER_SYNC_DEPTH_STENCIL,
	D3D12_BARRIER_SYNC_DEPTH_STENCIL,
	D3D12_BARRIER_SYNC_NONE,
};

static constexpr D3D12_BARRIER_ACCESS g_access_to_access_map[int(Gfx::Access::Count)]{
	D3D12_BARRIER_ACCESS_NO_ACCESS,
	D3D12_BARRIER_ACCESS_RENDER_TARGET,
	D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
	D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
	D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE,
	D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE,
	D3D12_BARRIER_ACCESS_NO_ACCESS,
};

static constexpr D3D12_BARRIER_LAYOUT g_access_to_layout_map[int(Gfx::Access::Count)]{
	D3D12_BARRIER_LAYOUT_UNDEFINED,
	D3D12_BARRIER_LAYOUT_RENDER_TARGET,
	D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
	D3D12_BARRIER_LAYOUT_SHADER_RESOURCE,
	D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
	D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE,
	D3D12_BARRIER_LAYOUT_PRESENT,
};

static constexpr S32 g_null_depth_target_index = -1;

struct RuntimeGraphPassDebugData
{
	StringView8 name;
};

struct RuntimeGraphClearColorCommand
{
	D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle;
	F32 color[4];
};

struct RuntimeGraphClearDepthCommand
{
	D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle;
	D3D12_CLEAR_FLAGS flags;
	F32 depth;
	U8 stencil;
};

struct FinalResource
{
	ID3D12Resource1* resources[g_frames_in_flight];
	DescriptorIndex<DescriptorType::Rtv> rtv_indices[g_frames_in_flight];
	DescriptorIndex<DescriptorType::CbvSrvUav> srv_indices[g_frames_in_flight];
	DescriptorIndex<DescriptorType::Dsv> dsv_indices[g_frames_in_flight];
	U64 total_size_bytes;
	U64 offset_bytes;
	D3D12_CLEAR_VALUE clear_value;
	bool needs_clear; // if not clear, discard
};

struct RuntimeRenderGraph;

struct ResourceInstanceRef;
struct RenderGraphPass;

struct Gfx::GraphPass
{
	StringView8 name;
	Gfx::GraphExecutor* executor;
	ResourceInstanceRef* first_write_ref;
	ResourceInstanceRef* current_write_ref;
	ResourceInstanceRef* first_read_ref;
	ResourceInstanceRef* current_read_ref;
	Gfx::GraphPass* next_pass;
	S32 index;
	S32 final_index;
};

struct PassRef
{
	Gfx::GraphPass* pass;
	PassRef* next_ref;
};

struct ResourceInstance
{
	Resource* resource;
	S32 ref_index;

	Gfx::GraphPass* writer_ref;
	PassRef* first_reader_ref;
	PassRef* current_reader_ref;
};

struct ResourceInstanceRef
{
	ResourceInstance* instance;
	ResourceInstanceRef* next;
	Gfx::Access access;
};

struct RuntimeGraphResource_t
{
};

struct RuntimeGraphResourceDebugData_t
{
	StringView8 name;
	PtrSize size_bytes;
	PtrSize offset_bytes;
	S32 start_pass_index;
	S32 end_pass_index;
};

struct RuntimeGraphPassInput_t
{
	S32 resource_index;
};

struct RuntimeGraphPassOutput_t
{
	S32 resource_index;
};

struct RenderGraphPass
{
	Gfx::GraphExecutor* executor;
	S32 inputs_start_index;
	S32 input_count;
	S32 outputs_start_index;
	S32 output_count;
	S32 barrier_groups_start_index;
	S32 barrier_group_count;
	S32 color_target_start_index;
	S32 color_target_count;
	S32 depth_target_index;
	S32 clear_color_start_index;
	S32 clear_color_count;
	S32 clear_depth_start_index;
	S32 clear_depth_count;
	S32 discard_start_index;
	S32 discard_count;
};

Gfx::GraphBuilder::GraphBuilder(Gfx::GraphTextureDesc in_backbuffer, IAllocator* allocator, IAllocator* final_graph_allocator)
	: allocator(allocator)
	, final_graph_allocator(final_graph_allocator)
{
	backbuffer = Resource{
		.name = in_backbuffer.name,
		.width = in_backbuffer.width,
		.height = in_backbuffer.height,
		.format = in_backbuffer.format,
		.sample_count = in_backbuffer.sample_count,
		.ref_count = -1,
		.index = 0,
		.initial_state = in_backbuffer.initial_state,
		.clear_value = in_backbuffer.clear_value,
		.origin = ResourceOrigin::Import,
	};

	backbuffer_instance = PAW_NEW_IN(allocator, ResourceInstance);
	backbuffer_instance->resource = &backbuffer;
	backbuffer_instance->ref_index = 0;

	external_resource_count = 1;
}

Gfx::GraphTexture Gfx::GraphBuilder::GetBackBuffer()
{
	return {reinterpret_cast<U64>(backbuffer_instance)};
}

Gfx::GraphTexture Gfx::GraphBuilder::CreateTexture(Gfx::GraphPass& pass, Gfx::GraphTextureDesc&& desc)
{
	PAW_ASSERT(desc.sample_count == 1, "Multisample is not currently supported");

	Resource* resource = PAW_NEW_IN(allocator, Resource)();
	resource->width = desc.width;
	resource->height = desc.height;
	resource->format = desc.format;
	resource->sample_count = desc.sample_count;
	resource->name = desc.name;
	resource->ref_count = 0;
	resource->index = resource_count++;
	resource->all_accesses = desc.access;
	resource->first_access = desc.access;
	resource->last_access = desc.access;
	resource->initial_state = desc.initial_state;
	resource->clear_value = desc.clear_value;

	ResourceInstance* ref = PAW_NEW_IN(allocator, ResourceInstance)();
	ref->resource = resource;
	ref->ref_index = resource->ref_count;

	ref->writer_ref = &pass;

	ResourceInstanceRef* instance_ref = PAW_NEW_IN(allocator, ResourceInstanceRef)();
	instance_ref->instance = ref;
	instance_ref->next = nullptr;
	instance_ref->access = desc.access;

	if (pass.first_write_ref == nullptr)
	{
		pass.first_write_ref = instance_ref;
	}
	else
	{
		pass.current_write_ref->next = instance_ref;
	}
	pass.current_write_ref = instance_ref;

	return {reinterpret_cast<U64>(ref)};
}

Gfx::GraphPass& Gfx::GraphBuilder::AddPass(StringView8 name)
{
	Gfx::GraphPass* pass = PAW_NEW_IN(allocator, Gfx::GraphPass)();
	pass->name = name;
	pass->next_pass = nullptr;
	pass->first_read_ref = nullptr;
	pass->current_read_ref = nullptr;
	pass->first_write_ref = nullptr;
	pass->current_write_ref = nullptr;
	pass->index = pass_count++;

	if (first_pass == nullptr)
	{
		first_pass = pass;
	}
	else
	{
		current_pass->next_pass = pass;
	}
	current_pass = pass;

	return *pass;
}

Gfx::GraphTexture Gfx::GraphBuilder::ReadTexture(Gfx::GraphPass& pass, Gfx::GraphTexture handle, Gfx::Access access)
{
	ResourceInstance* handle_instance = std::launder(reinterpret_cast<ResourceInstance*>(handle.handle));
	handle_instance->resource->all_accesses |= access;
	handle_instance->resource->last_access = access;

	PassRef* pass_ref = PAW_NEW_IN(allocator, PassRef)();
	pass_ref->next_ref = nullptr;
	pass_ref->pass = &pass;

	// assert comment might be wrong
	PAW_ASSERT(handle_instance->ref_index >= handle_instance->resource->ref_count, "handle is not latest to refer to a resource");

	if (handle_instance->first_reader_ref == nullptr)
	{
		handle_instance->first_reader_ref = pass_ref;
	}
	else
	{
		handle_instance->current_reader_ref->next_ref = pass_ref;
	}
	handle_instance->current_reader_ref = pass_ref;

	ResourceInstanceRef* instance_ref = PAW_NEW_IN(allocator, ResourceInstanceRef)();
	instance_ref->instance = handle_instance;
	instance_ref->next = nullptr;
	instance_ref->access = access;

	if (pass.first_read_ref == nullptr)
	{
		pass.first_read_ref = instance_ref;
	}
	else
	{
		pass.current_read_ref->next = instance_ref;
	}
	pass.current_read_ref = instance_ref;

	return handle;
}

Gfx::GraphTexture Gfx::GraphBuilder::WriteTexture(Gfx::GraphPass& pass, Gfx::GraphTexture handle, Gfx::Access access)
{
	ResourceInstance* handle_instance = std::launder(reinterpret_cast<ResourceInstance*>(handle.handle));
	handle_instance->resource->ref_count++;
	handle_instance->resource->all_accesses |= access;
	handle_instance->resource->last_access = access;

	PassRef* pass_ref = PAW_NEW_IN(allocator, PassRef)();
	pass_ref->next_ref = nullptr;
	pass_ref->pass = &pass;

	if (handle_instance->first_reader_ref == nullptr)
	{
		handle_instance->first_reader_ref = pass_ref;
	}
	else
	{
		handle_instance->current_reader_ref->next_ref = pass_ref;
	}
	handle_instance->current_reader_ref = pass_ref;

	ResourceInstance* ref = PAW_NEW_IN(allocator, ResourceInstance)();
	ref->resource = handle_instance->resource;
	ref->ref_index = handle_instance->resource->ref_count;

	ref->writer_ref = &pass;

	{

		ResourceInstanceRef* instance_ref = PAW_NEW_IN(allocator, ResourceInstanceRef)();
		instance_ref->instance = ref;
		instance_ref->next = nullptr;
		instance_ref->access = access;

		if (pass.first_write_ref == nullptr)
		{
			pass.first_write_ref = instance_ref;
		}
		else
		{
			pass.current_write_ref->next = instance_ref;
		}
		pass.current_write_ref = instance_ref;
	}

	{
		ResourceInstanceRef* instance_ref = PAW_NEW_IN(allocator, ResourceInstanceRef)();
		instance_ref->instance = handle_instance;
		instance_ref->next = nullptr;
		instance_ref->access = access;

		if (pass.first_read_ref == nullptr)
		{
			pass.first_read_ref = instance_ref;
		}
		else
		{
			pass.current_read_ref->next = instance_ref;
		}
		pass.current_read_ref = instance_ref;
	}

	return {reinterpret_cast<U64>(ref)};
}

void Gfx::GraphBuilder::SetExecutor(GraphPass& pass, GraphExecutor* executor)
{
	pass.executor = executor;
}

void Gfx::GraphBuilder::ProcessPass(Gfx::GraphPass* pass, Slice<S32> const& visited, Slice<S32> const& on_stack, Slice<Gfx::GraphPass const*> const& final_passes, S32& final_write_index)
{
	S32 const index = pass->index;
	if (visited[index])
	{
		PAW_ASSERT(!on_stack[index], "Circular dependency detected");
		return;
	}
	visited[index] = true;
	on_stack[index] = true;

	for (ResourceInstanceRef* write_ref = pass->first_write_ref; write_ref; write_ref = write_ref->next)
	{
		for (PassRef* read_ref = write_ref->instance->first_reader_ref; read_ref; read_ref = read_ref->next_ref)
		{
			Gfx::GraphPass* dep_pass = read_ref->pass;
			ProcessPass(dep_pass, visited, on_stack, final_passes, final_write_index);
		}
	}
	S32 const new_index = final_passes.count - final_write_index - 1;
	pass->final_index = new_index;
	final_passes[new_index] = pass;
	++final_write_index;
	on_stack[index] = false;
}

D3D12_RESOURCE_DESC1 CreateDesc(Resource const& resource)
{
	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

	if (resource.all_accesses.contains(Gfx::Access::RenderTarget))
	{
		flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	}

	if (resource.all_accesses.contains(Gfx::Access::Depth))
	{
		flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	}

	return D3D12_RESOURCE_DESC1{
		.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
		.Alignment = 0,
		.Width = static_cast<U32>(resource.width),
		.Height = UINT(resource.height),
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.Format = g_texture_format_map[int(resource.format)],
		.SampleDesc = {
			.Count = static_cast<U32>(resource.sample_count),
			.Quality = 0,
		},
		.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN, // #TODO: Maybe this should be in the first layout detected in the graph
		.Flags = flags,
	};
}

struct Gfx::Graph
{

	void Render(Gfx::CommandList command_list_handle, Gfx::State& gfx_state)
	{
		local_frame_index = gfx_state.local_frame_index;
		ID3D12DescriptorHeap* const descriptor_heaps[]{
			gfx_state.descriptor_pool.GetCbvSrvUavHeap(),
			gfx_state.descriptor_pool.GetSamplerHeap(),
		};
		ID3D12GraphicsCommandList9* command_list = gfx_state.command_list_allocator.GetGraphicsCommandList(command_list_handle);
		command_list->SetDescriptorHeaps(PAW_ARRAY_COUNT(descriptor_heaps), descriptor_heaps);

		for (S32 pass_index = 0; pass_index < passes.count; pass_index++)
		{
			RenderGraphPass const& pass = passes[pass_index];
			RuntimeGraphPassDebugData const& pass_debug_data = passes_debug_data[pass_index];
			//   PAW_ASSERT(pass_debug_data.name.null_terminated, ");
			PIXBeginEvent(command_list, 0, PAW_STR_FMT, PAW_FMT_STR(pass_debug_data.name));
			//    TracyD3D12ZoneTransient(profiler_ctx, tracy_d3d12_zone, command_list, pass_debug_data.name.ptr, true);
			if (pass.barrier_group_count > 0)
			{
				command_list->Barrier(pass.barrier_group_count, &barrier_groups[local_frame_index][pass.barrier_groups_start_index]);
			}
			if (pass.output_count > 0)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE const* const depth_target_handle = pass.depth_target_index != g_null_depth_target_index ? &depth_target_handles[local_frame_index][pass.depth_target_index] : nullptr;
				command_list->OMSetRenderTargets(pass.color_target_count, &color_target_handles[local_frame_index][pass.color_target_start_index], false, depth_target_handle);
				command_list->RSSetViewports(pass.output_count, &viewports[pass.outputs_start_index]);
				command_list->RSSetScissorRects(pass.output_count, &scissors[pass.outputs_start_index]);
			}
			for (S32 i = pass.clear_color_start_index; i < pass.clear_color_start_index + pass.clear_color_count; i++)
			{
				RuntimeGraphClearColorCommand const& clear_command = clear_color_commands[local_frame_index][i];
				command_list->ClearRenderTargetView(clear_command.rtv_handle, clear_command.color, 0, nullptr);
			}

			for (S32 i = pass.clear_depth_start_index; i < pass.clear_depth_start_index + pass.clear_depth_count; i++)
			{
				RuntimeGraphClearDepthCommand const& clear_command = clear_depth_commands[local_frame_index][i];
				command_list->ClearDepthStencilView(clear_command.dsv_handle, clear_command.flags, clear_command.depth, clear_command.stencil, 0, nullptr);
			}

			for (S32 i = pass.discard_start_index; i < pass.discard_start_index + pass.discard_count; i++)
			{
				command_list->DiscardResource(discard_commands[local_frame_index][i], nullptr);
			}

			pass.executor->Execute(gfx_state, command_list_handle);
			PIXEndEvent(command_list);
		}
	}

	DescriptorIndex<DescriptorType::CbvSrvUav> GetSrv(Gfx::GraphTexture resource)
	{
		ResourceInstance* handle_instance = std::launder(reinterpret_cast<ResourceInstance*>(resource.handle));

		FinalResource& final_resource = final_resources[handle_instance->resource->index];
		return final_resource.srv_indices[local_frame_index];
	}

	Slice<FinalResource> final_resources;
	Slice<FinalResource> external_final_resources;
	Slice<RuntimeGraphResource_t> resources;
	Slice<RenderGraphPass> passes;
	Slice<RuntimeGraphPassInput_t> inputs;
	Slice<RuntimeGraphPassOutput_t> outputs;
	Slice<D3D12_VIEWPORT> viewports;
	Slice<D3D12_RECT> scissors;

	Slice2D<D3D12_BARRIER_GROUP> barrier_groups;				 // Per-frame
	Slice2D<D3D12_TEXTURE_BARRIER> texture_barriers;			 // Per-frame
	Slice2D<D3D12_CPU_DESCRIPTOR_HANDLE> color_target_handles;	 // Per-frame
	Slice2D<D3D12_CPU_DESCRIPTOR_HANDLE> depth_target_handles;	 // Per-frame
	Slice2D<RuntimeGraphClearColorCommand> clear_color_commands; // Per-frame
	Slice2D<RuntimeGraphClearDepthCommand> clear_depth_commands; // Per-frame
	Slice2D<ID3D12Resource1*> discard_commands;					 // Per-frame

	Slice<RuntimeGraphPassDebugData> passes_debug_data;
	Slice<RuntimeGraphResourceDebugData_t> resources_debug_data;

	ID3D12Heap* heap;
	U64 total_frame_heap_size_bytes;
	U64 total_heap_size_bytes;

	S32 local_frame_index;
};

Gfx::Graph* Gfx::GraphBuilder::Build(Gfx::State& gfx_state)
{

	Slice<S32> const visited_passes = PAW_NEW_SLICE_IN(allocator, pass_count, S32);
	Slice<S32> const on_stack = PAW_NEW_SLICE_IN(allocator, pass_count, S32);
	Slice<Gfx::GraphPass const*> const topological_order = PAW_NEW_SLICE_IN(allocator, pass_count, Gfx::GraphPass const*);
	S32 final_write_index = 0;

	for (Gfx::GraphPass* pass = first_pass; pass; pass = pass->next_pass)
	{
		ProcessPass(pass, visited_passes, on_stack, topological_order, final_write_index);
	}

	Slice<S32> const distances = PAW_NEW_SLICE_IN(allocator, pass_count, S32);
	for (Gfx::GraphPass const* pass : topological_order)
	{
		for (ResourceInstanceRef* write_ref = pass->first_write_ref; write_ref; write_ref = write_ref->next)
		{
			for (PassRef* read_ref = write_ref->instance->first_reader_ref; read_ref; read_ref = read_ref->next_ref)
			{
				Gfx::GraphPass* dep_pass = read_ref->pass;
				if (distances[dep_pass->final_index] < distances[pass->final_index] + 1)
				{
					distances[dep_pass->final_index] = distances[pass->final_index] + 1;
				}
			}
		}
	}

	struct ResourceLifetime
	{
		Resource* resource = nullptr;
		S32 start_pass_index = 0;
		S32 end_pass_index = 0;

		PtrSize total_size_bytes = 0;
		PtrSize alignment_bytes = 0;
	};

	// PAW_LOG_INFO("====================================================================================================");

	Slice<ResourceLifetime> const resource_lifetimes = PAW_NEW_SLICE_IN(allocator, resource_count, ResourceLifetime);
	Slice<ResourceLifetime> const external_resource_lifetimes = PAW_NEW_SLICE_IN(allocator, external_resource_count, ResourceLifetime);
	for (ResourceLifetime& lifetime : resource_lifetimes)
	{
		lifetime.start_pass_index = pass_count;
	}
	for (ResourceLifetime& lifetime : external_resource_lifetimes)
	{
		lifetime.start_pass_index = pass_count;
	}

	// Calculate resource lifetimes
	for (Gfx::GraphPass const* pass : topological_order)
	{
		S32 const pass_index = pass->final_index;
		for (ResourceInstanceRef* write_ref = pass->first_write_ref; write_ref; write_ref = write_ref->next)
		{
			Resource* resource = write_ref->instance->resource;
			ResourceLifetime& lifetime = resource->origin == ResourceOrigin::Graph ? resource_lifetimes[resource->index] : external_resource_lifetimes[resource->index];
			lifetime.resource = resource;
			if (pass_index < lifetime.start_pass_index)
			{
				lifetime.start_pass_index = pass_index;
			}

			if (pass_index > lifetime.end_pass_index)
			{
				lifetime.end_pass_index = pass_index;
			}
		}

		for (ResourceInstanceRef* read_ref = pass->first_read_ref; read_ref; read_ref = read_ref->next)
		{
			Resource* resource = read_ref->instance->resource;
			ResourceLifetime& lifetime = resource->origin == ResourceOrigin::Graph ? resource_lifetimes[resource->index] : external_resource_lifetimes[resource->index];
			if (pass_index < lifetime.start_pass_index)
			{
				lifetime.start_pass_index = pass_index;
			}

			if (pass_index > lifetime.end_pass_index)
			{
				lifetime.end_pass_index = pass_index;
			}
		}
	}
	PtrSize total_frame_heap_size_bytes = 0;
	Slice<FinalResource> final_resources = PAW_NEW_SLICE_IN(final_graph_allocator, resource_count + external_resource_count, FinalResource);

	{
		Slice<ResourceLifetime> const sorted_resource_lifetimes = PAW_NEW_SLICE_IN(allocator, resource_lifetimes.count, ResourceLifetime);
		std::memcpy(sorted_resource_lifetimes.items, resource_lifetimes.items, CalcTotalSizeBytes(sorted_resource_lifetimes));

		for (ResourceLifetime& lifetime : sorted_resource_lifetimes)
		{
			Resource* const resource = lifetime.resource;

			D3D12_RESOURCE_DESC1 const desc = CreateDesc(*resource);
			D3D12_RESOURCE_ALLOCATION_INFO const alloc_info = gfx_state.device->GetResourceAllocationInfo2(0, 1, &desc, nullptr);
			lifetime.total_size_bytes = alloc_info.SizeInBytes;
			lifetime.alignment_bytes = alloc_info.Alignment;
		}

		std::sort(sorted_resource_lifetimes.items, sorted_resource_lifetimes.items + sorted_resource_lifetimes.count, [](ResourceLifetime& a, ResourceLifetime& b)
				  { return a.total_size_bytes > b.total_size_bytes; });

		S32 resource_lifetime_count = sorted_resource_lifetimes.count;

		struct Bucket
		{
			S32 start_pass_index;
			S32 end_pass_index;

			Resource* resource;
			Gfx::AccessFlags all_accesses;

			S32 start_region_index;
			S32 region_count;

			PtrSize size_bytes;
		};

		struct Region
		{
			S32 start_pass_index;
			S32 end_pass_index;

			Resource* resource;
			Gfx::AccessFlags all_accesses;

			PtrSize offset_bytes;
			PtrSize size_bytes;
		};

		struct UnaliasableOffset
		{
			enum class Type
			{
				Start,
				End,
			};
			PtrSize offset_bytes;
			Type type;
		};

		Slice<Bucket> const buckets = PAW_NEW_SLICE_IN(allocator, resource_count, Bucket);
		Slice<Region> const regions = PAW_NEW_SLICE_IN(allocator, resource_count, Region);
		Slice<UnaliasableOffset> const offsets_scratch = PAW_NEW_SLICE_IN(allocator, resource_count, UnaliasableOffset);
		S32 bucket_count = 0;

		static constexpr auto lifetime_intersects = [](S32 a_start, S32 a_end, S32 b_start, S32 b_end) -> bool
		{
			return a_end >= b_start && b_end >= a_start;
		};

		{
			S32 region_offset = 0;
			for (S32 bucket_lifetime_index = 0; bucket_lifetime_index < resource_lifetime_count; ++bucket_lifetime_index)
			{
				ResourceLifetime const& first_lifetime = sorted_resource_lifetimes[bucket_lifetime_index];
				buckets[bucket_count++] = Bucket{
					.start_pass_index = first_lifetime.start_pass_index,
					.end_pass_index = first_lifetime.end_pass_index,
					.resource = first_lifetime.resource,
					.all_accesses = first_lifetime.resource->all_accesses,
					.start_region_index = region_offset,
					.region_count = 0,
					.size_bytes = first_lifetime.total_size_bytes,
				};

				Bucket& bucket = buckets[bucket_count - 1];

				for (S32 lifetime_index = bucket_lifetime_index + 1; lifetime_index < resource_lifetime_count; ++lifetime_index)
				{
					ResourceLifetime const& lifetime = sorted_resource_lifetimes[lifetime_index];
					bool const intersects_bucket = lifetime_intersects(lifetime.start_pass_index, lifetime.end_pass_index, bucket.start_pass_index, bucket.end_pass_index);
					if (intersects_bucket)
					{
						continue;
					}

					S32 unaliasable_offset_count = 0;
					offsets_scratch[unaliasable_offset_count++] = {0, UnaliasableOffset::Type::End};
					offsets_scratch[unaliasable_offset_count++] = {bucket.size_bytes, UnaliasableOffset::Type::Start};

					for (S32 region_index = bucket.start_region_index; region_index < bucket.start_region_index + bucket.region_count; ++region_index)
					{
						Region const& region = regions[region_index];
						// We can alias the region so we don't need to mark it as unaliasable
						if (!lifetime_intersects(lifetime.start_pass_index, lifetime.end_pass_index, region.start_pass_index, region.end_pass_index))
						{
							continue;
						}

						offsets_scratch[unaliasable_offset_count++] = {region.offset_bytes, UnaliasableOffset::Type::Start};
						offsets_scratch[unaliasable_offset_count++] = {region.offset_bytes + region.size_bytes, UnaliasableOffset::Type::End};
					}

					std::sort(offsets_scratch.items, offsets_scratch.items + unaliasable_offset_count, [](UnaliasableOffset const& a, UnaliasableOffset const& b)
							  { return b.offset_bytes > a.offset_bytes; });

					S32 overlap_counter = 0;
					PtrSize smallest_region_size_bytes = bucket.size_bytes + 1;
					PtrSize smallest_region_offset_bytes = 0;
					bool found_region = false;
					for (S32 i = 0; i < unaliasable_offset_count - 1; ++i)
					{
						UnaliasableOffset const& offset = offsets_scratch[i];
						UnaliasableOffset const& next_offset = offsets_scratch[i + 1];
						if (offset.type == UnaliasableOffset::Type::End && next_offset.type == UnaliasableOffset::Type::Start && overlap_counter == 0)
						{
							PtrSize const region_size = next_offset.offset_bytes - offset.offset_bytes;
							if (lifetime.total_size_bytes <= region_size && region_size < smallest_region_size_bytes)
							{
								found_region = true;
								smallest_region_size_bytes = region_size;
								smallest_region_offset_bytes = offset.offset_bytes;
							}
						}

						if (offset.type == UnaliasableOffset::Type::Start)
						{
							overlap_counter++;
						}
						else if (offset.type == UnaliasableOffset::Type::End)
						{
							overlap_counter--;
						}
					}

					if (found_region)
					{
						S32 const region_index = bucket.start_region_index + bucket.region_count++;
						regions[region_index] = {
							.start_pass_index = lifetime.start_pass_index,
							.end_pass_index = lifetime.end_pass_index,
							.resource = lifetime.resource,
							.all_accesses = lifetime.resource->all_accesses,
							.offset_bytes = smallest_region_offset_bytes,
							.size_bytes = lifetime.total_size_bytes,
						};

						// PAW_LOG_INFO("Wrote region: %llu", region_index);

						PAW_ASSERT(smallest_region_offset_bytes % lifetime.alignment_bytes == 0, "Region does not match required alignment");

						sorted_resource_lifetimes[lifetime_index] = sorted_resource_lifetimes[resource_lifetime_count - 1];
						resource_lifetime_count--;
						std::sort(sorted_resource_lifetimes.items, sorted_resource_lifetimes.items + resource_lifetime_count, [](ResourceLifetime& a, ResourceLifetime& b)
								  { return a.total_size_bytes > b.total_size_bytes; });
					}
				}
				region_offset += bucket.region_count;
			}
		}

		PtrSize largest_image_size = 0;
		for (S32 i = 0; i < bucket_count; ++i)
		{
			total_frame_heap_size_bytes += buckets[i].size_bytes;
			largest_image_size = Max(largest_image_size, buckets[i].size_bytes);
		}

		PAW_INFO("Assigning heap offsets:\n");
		{
			PtrSize bucket_offset_bytes = 0;
			for (S32 bucket_index = 0; bucket_index < bucket_count; ++bucket_index)
			{
				Bucket const& bucket = buckets[bucket_index];

				{
					FinalResource& final_resource = final_resources[bucket.resource->index];

					PAW_INFO("\tAssigned B %llu: %llu to %d:\n", bucket.size_bytes, bucket_offset_bytes, bucket.resource->index);
					final_resource.total_size_bytes = bucket.size_bytes;
					final_resource.offset_bytes = bucket_offset_bytes;
				}

				for (S32 i = 0; i < bucket.region_count; ++i)
				{
					S32 const region_index = bucket.start_region_index + i;
					Region const& region = regions[region_index];
					Resource const* const resource = region.resource;

					FinalResource& final_resource = final_resources[resource->index];

					PAW_INFO("\tAssigned R %llu: %llu to %d:\n", region.size_bytes, bucket_offset_bytes + region.offset_bytes, resource->index);
					final_resource.total_size_bytes = region.size_bytes;
					final_resource.offset_bytes = bucket_offset_bytes + region.offset_bytes;
				}
				bucket_offset_bytes += bucket.size_bytes;
			}
		}
	}

	PtrSize const total_heap_size_bytes = total_frame_heap_size_bytes * g_frames_in_flight;

	D3D12_HEAP_DESC const heap_desc{
		.SizeInBytes = total_heap_size_bytes,
		.Properties = {
			.Type = D3D12_HEAP_TYPE_DEFAULT,
			.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask = 1,
			.VisibleNodeMask = 1,
		},
		.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
		.Flags = D3D12_HEAP_FLAG_NONE,
	};

	ID3D12Heap* heap = nullptr;
	DX_VERIFY(gfx_state.device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap)));

	U32 total_clear_color_count = 0;
	U32 total_clear_depth_count = 0;
	U32 total_discard_count = 0;

	PAW_INFO("Creating d3d resources for graph textures:\n");

	final_resources[resource_count + 0] = {
		.total_size_bytes = 0,
		.offset_bytes = 0,
	};

	std::memcpy(final_resources[resource_count + 0].resources, gfx_state.backbuffer_resources, sizeof(ID3D12Resource*) * g_frames_in_flight);

	for (S32 resource_index = 0; resource_index < final_resources.count; resource_index++)
	{
		FinalResource& final_resource = final_resources[resource_index];
		Resource const* const resource = resource_index < resource_count ? resource_lifetimes[resource_index].resource : external_resource_lifetimes[resource_index - resource_count].resource;
		PAW_INFO("\tCreating %d\n", resource->index);

		D3D12_RESOURCE_DESC1 const resource_desc = CreateDesc(*resource);

		D3D12_CLEAR_VALUE clear_value{
			.Format = resource_desc.Format,
		};

		std::memcpy(&clear_value.Color, &resource->clear_value, sizeof(resource->clear_value));

		bool const needs_clear_value = resource->NeedsClearValue();

		final_resource.needs_clear = needs_clear_value;
		final_resource.clear_value = clear_value;

		if (resource->origin == ResourceOrigin::Graph)
		{
			for (S32 frame_index = 0; frame_index < g_frames_in_flight; frame_index++)
			{
				DX_VERIFY(gfx_state.device->CreatePlacedResource2(heap, final_resource.offset_bytes + total_frame_heap_size_bytes * frame_index, &resource_desc, g_access_to_layout_map[U32(resource->last_access)], needs_clear_value ? &clear_value : nullptr, 0, nullptr, IID_PPV_ARGS(&final_resource.resources[frame_index])));
				SetDebugName(final_resource.resources[frame_index], resource->name);
			}
		}

		if (resource->all_accesses.contains(Gfx::Access::RenderTarget))
		{
			D3D12_RENDER_TARGET_VIEW_DESC const desc{
				.Format = resource_desc.Format,
				.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
			};
			for (S32 frame_index = 0; frame_index < g_frames_in_flight; frame_index++)
			{
				final_resource.rtv_indices[frame_index] = gfx_state.descriptor_pool.AllocRtv();
				gfx_state.device->CreateRenderTargetView(final_resource.resources[frame_index], &desc, gfx_state.descriptor_pool.GetRtvCPUHandle(final_resource.rtv_indices[frame_index]));
			}

			if (final_resource.needs_clear)
			{
				total_clear_color_count++;
			}
		}
		else
		{
			for (S32 frame_index = 0; frame_index < g_frames_in_flight; frame_index++)
			{
				final_resource.rtv_indices[frame_index] = g_null_rtv_descriptor_index;
			}
		}

		if (resource->all_accesses.contains(Gfx::Access::VertexShader) || resource->all_accesses.contains(Gfx::Access::PixelShader))
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC const desc{
				.Format = g_srv_format_map[U32(resource->format)],
				.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
				.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
				.Texture2D = {
					.MostDetailedMip = 0,
					.MipLevels = resource_desc.MipLevels,
				},
			};
			for (S32 frame_index = 0; frame_index < g_frames_in_flight; frame_index++)
			{
				final_resource.srv_indices[frame_index] = gfx_state.descriptor_pool.AllocCbvSrvUav();
				gfx_state.device->CreateShaderResourceView(final_resource.resources[frame_index], &desc, gfx_state.descriptor_pool.GetCbvSrvUavCPUHandle(final_resource.srv_indices[frame_index]));
			}
		}
		else
		{
			for (S32 frame_index = 0; frame_index < g_frames_in_flight; frame_index++)
			{
				final_resource.srv_indices[frame_index] = g_null_cbv_srv_uav_descriptor_index;
			}
		}

		if (resource->all_accesses.contains(Gfx::Access::Depth))
		{
			D3D12_DEPTH_STENCIL_VIEW_DESC const desc{
				.Format = resource_desc.Format,
				.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
				.Flags = D3D12_DSV_FLAG_NONE, // #TODO: re-evaluate if this can be narrowed down
			};
			for (S32 frame_index = 0; frame_index < g_frames_in_flight; frame_index++)
			{
				final_resource.dsv_indices[frame_index] = gfx_state.descriptor_pool.AllocDsv();
				gfx_state.device->CreateDepthStencilView(final_resource.resources[frame_index], &desc, gfx_state.descriptor_pool.GetDsvCPUHandle(final_resource.dsv_indices[frame_index]));
			}
			if (final_resource.needs_clear)
			{
				total_clear_depth_count++;
			}
		}
		else
		{
			for (S32 frame_index = 0; frame_index < g_frames_in_flight; frame_index++)
			{
				final_resource.dsv_indices[frame_index] = g_null_dsv_descriptor_index;
			}
		}

		if (!final_resource.needs_clear)
		{
			total_discard_count++;
		}
	}

	U32 total_input_count = 0;
	U32 total_output_count = 0;
	U32 total_color_target_count = 0;
	U32 total_depth_target_count = 0;

	for (Gfx::GraphPass const* const pass : topological_order)
	{
		for (ResourceInstanceRef* read_ref = pass->first_read_ref; read_ref; read_ref = read_ref->next)
		{
			total_input_count++;
		}

		for (ResourceInstanceRef* write_ref = pass->first_write_ref; write_ref; write_ref = write_ref->next)
		{
			if (write_ref->access == Gfx::Access::RenderTarget)
			{
				total_color_target_count++;
			}

			if (write_ref->access == Gfx::Access::Depth)
			{
				total_depth_target_count++;
			}

			total_output_count++;
		}
	}

	Gfx::Graph& runtime_graph = *PAW_NEW_IN(final_graph_allocator, Gfx::Graph);

	runtime_graph.passes = PAW_NEW_SLICE_IN(final_graph_allocator, pass_count, RenderGraphPass);
	runtime_graph.inputs = PAW_NEW_SLICE_IN(final_graph_allocator, total_input_count, RuntimeGraphPassInput_t);
	runtime_graph.outputs = PAW_NEW_SLICE_IN(final_graph_allocator, total_output_count, RuntimeGraphPassOutput_t);
	runtime_graph.viewports = PAW_NEW_SLICE_IN(final_graph_allocator, total_output_count, D3D12_VIEWPORT);
	runtime_graph.scissors = PAW_NEW_SLICE_IN(final_graph_allocator, total_output_count, D3D12_RECT);

	// Per-frame data
	runtime_graph.barrier_groups = PAW_NEW_SLICE_2D_IN(final_graph_allocator, g_frames_in_flight, pass_count * 3, D3D12_BARRIER_GROUP);
	runtime_graph.texture_barriers = PAW_NEW_SLICE_2D_IN(final_graph_allocator, g_frames_in_flight, total_input_count + total_output_count, D3D12_TEXTURE_BARRIER);
	runtime_graph.color_target_handles = PAW_NEW_SLICE_2D_IN(final_graph_allocator, g_frames_in_flight, total_color_target_count, D3D12_CPU_DESCRIPTOR_HANDLE);
	runtime_graph.depth_target_handles = PAW_NEW_SLICE_2D_IN(final_graph_allocator, g_frames_in_flight, total_depth_target_count, D3D12_CPU_DESCRIPTOR_HANDLE);
	runtime_graph.clear_color_commands = PAW_NEW_SLICE_2D_IN(final_graph_allocator, g_frames_in_flight, total_clear_color_count, RuntimeGraphClearColorCommand);
	runtime_graph.clear_depth_commands = PAW_NEW_SLICE_2D_IN(final_graph_allocator, g_frames_in_flight, total_clear_depth_count, RuntimeGraphClearDepthCommand);
	runtime_graph.discard_commands = PAW_NEW_SLICE_2D_IN(final_graph_allocator, g_frames_in_flight, total_discard_count, ID3D12Resource1*);

	// Debug data
	// #TODO: Allocate this on a debug allocator
	runtime_graph.passes_debug_data = PAW_NEW_SLICE_IN(final_graph_allocator, pass_count, RuntimeGraphPassDebugData);
	runtime_graph.resources_debug_data = PAW_NEW_SLICE_IN(final_graph_allocator, final_resources.count, RuntimeGraphResourceDebugData_t);

	runtime_graph.heap = heap;
	runtime_graph.total_heap_size_bytes = total_heap_size_bytes;
	runtime_graph.total_frame_heap_size_bytes = total_frame_heap_size_bytes;

	struct ResourceTrackingState_t
	{
		D3D12_BARRIER_SYNC last_sync;
		D3D12_BARRIER_ACCESS last_access;
		D3D12_BARRIER_LAYOUT last_layout;

		static ResourceTrackingState_t create(Gfx::AccessFlags flags)
		{
			ResourceTrackingState_t result{};
			result.last_sync = D3D12_BARRIER_SYNC_NONE;
			result.last_access = D3D12_BARRIER_ACCESS_COMMON;
			result.last_layout = D3D12_BARRIER_LAYOUT_UNDEFINED;
			for (Gfx::Access access = Gfx::Access::RenderTarget; access < Gfx::Access::Count; access = Gfx::Access(U32(access) + 1))
			{
				U32 const index = U32(access);
				if (flags.contains(access))
				{
					result.last_sync = g_access_to_sync_map[index];
					result.last_access |= g_access_to_access_map[index];
					result.last_layout = g_access_to_layout_map[index];
				}
			}

			return result;
		}

		static ResourceTrackingState_t Create(Gfx::Access access)
		{
			U32 const index = U32(access);
			return ResourceTrackingState_t{
				.last_sync = g_access_to_sync_map[index],
				.last_access = g_access_to_access_map[index],
				.last_layout = g_access_to_layout_map[index],
			};
		}
	};

	Slice<ResourceTrackingState_t> const trackers = PAW_NEW_SLICE_IN(allocator, final_resources.count, ResourceTrackingState_t);

	for (S32 i = 0; i < final_resources.count; i++)
	{
		ResourceTrackingState_t& tracker = trackers[i];
		ResourceLifetime const& lifetime = i < resource_count ? resource_lifetimes[i] : external_resource_lifetimes[i - resource_count];
		Resource* const resource = lifetime.resource;
		resource->index = i;
		FinalResource const& final_resource = final_resources[i];
		// Update to last access so that we emulate a needing to transition from a previous frame
		tracker = ResourceTrackingState_t::Create(resource->last_access);

		RuntimeGraphResourceDebugData_t& debug_data = runtime_graph.resources_debug_data[i];
		debug_data.name = resource->name;
		debug_data.size_bytes = final_resource.total_size_bytes;
		debug_data.offset_bytes = final_resource.offset_bytes;
		debug_data.start_pass_index = S32(lifetime.start_pass_index);
		debug_data.end_pass_index = S32(lifetime.end_pass_index);
	}

	{
		S32 pass_index = 0;
		// S32 resource_index = 0;
		S32 input_index = 0;
		S32 output_index = 0;
		S32 texture_barrier_index = 0;
		S32 barrier_group_index = 0;
		S32 color_target_index = 0;
		S32 depth_target_index = 0;
		S32 viewport_index = 0;
		S32 scissor_index = 0;
		S32 clear_color_index = 0;
		S32 clear_depth_index = 0;
		S32 discard_index = 0;

		for (Gfx::GraphPass const* const pass : topological_order)
		{
			S32 const texture_barrier_start_index = texture_barrier_index;
			S32 const output_start_index = output_index;
			S32 const color_target_start_index = color_target_index;
			S32 depth_target_start_index = g_null_depth_target_index;
			S32 const clear_color_start_index = clear_color_index;
			S32 const clear_depth_start_index = clear_depth_index;
			S32 const discard_start_index = discard_index;

			for (ResourceInstanceRef* write_ref = pass->first_write_ref; write_ref; write_ref = write_ref->next)
			{
				static byte buffer[512];
				Resource* resource = write_ref->instance->resource;
				ResourceLifetime const& resource_lifetime = resource->origin == ResourceOrigin::Graph ? resource_lifetimes[resource->index] : external_resource_lifetimes[resource->index - resource_count];
				FinalResource& final_resource = final_resources[resource->index];
				UINT name_length = PAW_ARRAY_COUNT(buffer);
				final_resource.resources[0]->GetPrivateData(WKPDID_D3DDebugObjectName, &name_length, buffer);
				// PAW_LOG_INFO("Write %p{str} == %.*s", &resource->name, name_length, buffer);
				RuntimeGraphPassOutput_t& output = runtime_graph.outputs[output_index++];
				output.resource_index = resource->index;

				ResourceTrackingState_t const new_tracker = ResourceTrackingState_t::Create(write_ref->access);

				ResourceTrackingState_t const& current_tracker = trackers[resource->index];

				if (current_tracker.last_sync != new_tracker.last_sync || current_tracker.last_access != new_tracker.last_access || current_tracker.last_layout != new_tracker.last_layout)
				{
					S32 const write_index = texture_barrier_index++;
					for (S32 i = 0; i < g_frames_in_flight; i++)
					{
						D3D12_TEXTURE_BARRIER& texture_barrier = runtime_graph.texture_barriers[i][write_index];
						texture_barrier.SyncBefore = current_tracker.last_sync;
						texture_barrier.SyncAfter = new_tracker.last_sync;
						texture_barrier.AccessBefore = current_tracker.last_access;
						texture_barrier.AccessAfter = new_tracker.last_access;
						texture_barrier.LayoutBefore = current_tracker.last_layout;
						texture_barrier.LayoutAfter = new_tracker.last_layout;
						texture_barrier.pResource = final_resource.resources[i];
						// #TODO: Properly handle subresources
						texture_barrier.Subresources = {
							.IndexOrFirstMipLevel = 0,
							.NumMipLevels = 1,
							.FirstArraySlice = 0,
							.NumArraySlices = 1,
							.FirstPlane = 0,
							.NumPlanes = 1,
						};
						texture_barrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
					}
				}
				trackers[resource->index] = new_tracker;

				// bool const is_first_reference = write_ref->instance->ref_index == 0;
				bool const is_first_reference = pass->final_index == resource_lifetime.start_pass_index;

				if (write_ref->access == Gfx::Access::RenderTarget)
				{
					S32 const color_target_write_index = color_target_index++;
					bool const needs_clear_command = is_first_reference && final_resource.needs_clear;
					S32 const clear_command_write_index = needs_clear_command ? clear_color_index++ : 0;
					for (S32 i = 0; i < g_frames_in_flight; i++)
					{
						D3D12_CPU_DESCRIPTOR_HANDLE const cpu_handle = gfx_state.descriptor_pool.GetRtvCPUHandle(final_resource.rtv_indices[i]);

						runtime_graph.color_target_handles[i][color_target_write_index] = cpu_handle;

						if (needs_clear_command)
						{
							RuntimeGraphClearColorCommand command{
								.rtv_handle = cpu_handle,
							};

							std::memcpy(&command.color, &final_resource.clear_value.Color, sizeof(command.color));

							runtime_graph.clear_color_commands[i][clear_command_write_index] = command;
						}
					}
				}
				else if (write_ref->access == Gfx::Access::Depth)
				{
					PAW_ASSERT(depth_target_start_index == g_null_depth_target_index, "Depth target start index is null");
					depth_target_start_index = depth_target_index++;
					bool const needs_clear_command = is_first_reference && final_resource.needs_clear;
					S32 const clear_depth_write_index = needs_clear_command ? clear_depth_index++ : 0;
					for (S32 i = 0; i < g_frames_in_flight; i++)
					{
						D3D12_CPU_DESCRIPTOR_HANDLE const cpu_handle = gfx_state.descriptor_pool.GetDsvCPUHandle(final_resource.dsv_indices[i]);

						runtime_graph.depth_target_handles[i][depth_target_start_index] = cpu_handle;

						if (needs_clear_command)
						{
							runtime_graph.clear_depth_commands[i][clear_depth_write_index] = RuntimeGraphClearDepthCommand{
								.dsv_handle = cpu_handle,
								.flags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, // #TODO: Handle this properly
								.depth = final_resource.clear_value.DepthStencil.Depth,
								.stencil = final_resource.clear_value.DepthStencil.Stencil,
							};
						}
					}
				}

				if (is_first_reference && !final_resource.needs_clear)
				{
					S32 const write_index = discard_index++;
					for (S32 i = 0; i < g_frames_in_flight; i++)
					{
						runtime_graph.discard_commands[i][write_index] = final_resource.resources[i];
					}
				}

				// #TODO: Specify min / max depth values
				runtime_graph.viewports[viewport_index++] = D3D12_VIEWPORT{
					.TopLeftX = 0.0f,
					.TopLeftY = 0.0f,
					.Width = F32(resource->width),
					.Height = F32(resource->height),
					.MinDepth = 0.0f,
					.MaxDepth = 1.0f,
				};
				runtime_graph.scissors[scissor_index++] = D3D12_RECT{
					.left = 0,
					.top = 0,
					.right = LONG(resource->width),
					.bottom = LONG(resource->height),
				};
			}

			S32 const input_start_index = input_index;

			for (ResourceInstanceRef* read_ref = pass->first_read_ref; read_ref; read_ref = read_ref->next)
			{
				static byte buffer[512];
				Resource* resource = read_ref->instance->resource;
				FinalResource& final_resource = final_resources[resource->index];
				UINT name_length = PAW_ARRAY_COUNT(buffer);
				final_resource.resources[0]->GetPrivateData(WKPDID_D3DDebugObjectName, &name_length, buffer);
				// PAW_LOG_INFO("Read %p{str} == %.*s", &resource->name, name_length, buffer);
				RuntimeGraphPassInput_t& input = runtime_graph.inputs[input_index++];
				input.resource_index = resource->index;

				ResourceTrackingState_t const new_tracker = ResourceTrackingState_t::Create(read_ref->access);

				ResourceTrackingState_t const& current_tracker = trackers[resource->index];

				if (current_tracker.last_sync != new_tracker.last_sync || current_tracker.last_access != new_tracker.last_access || current_tracker.last_layout != new_tracker.last_layout)
				{
					S32 const texture_barrier_write_index = texture_barrier_index++;
					for (S32 i = 0; i < g_frames_in_flight; i++)
					{
						D3D12_TEXTURE_BARRIER& texture_barrier = runtime_graph.texture_barriers[i][texture_barrier_write_index];
						texture_barrier.SyncBefore = current_tracker.last_sync;
						texture_barrier.SyncAfter = new_tracker.last_sync;
						texture_barrier.AccessBefore = current_tracker.last_access;
						texture_barrier.AccessAfter = new_tracker.last_access;
						texture_barrier.LayoutBefore = current_tracker.last_layout;
						texture_barrier.LayoutAfter = new_tracker.last_layout;
						texture_barrier.pResource = final_resource.resources[i];
						// #TODO: Properly handle subresources
						texture_barrier.Subresources = {
							.IndexOrFirstMipLevel = 0,
							.NumMipLevels = 1,
							.FirstArraySlice = 0,
							.NumArraySlices = 1,
							.FirstPlane = 0,
							.NumPlanes = 1,
						};
						texture_barrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
					}
				}
				trackers[resource->index] = new_tracker;
			}

			S32 const input_count = input_index - input_start_index;
			S32 const output_count = output_index - output_start_index;
			S32 const texture_barrier_count = texture_barrier_index - texture_barrier_start_index;
			S32 const barrier_groups_start_index = barrier_group_index;
			S32 const color_target_count = color_target_index - color_target_start_index;

			S32 barrier_group_count = 0;

			if (texture_barrier_count > 0)
			{
				S32 const write_index = barrier_group_index++;
				for (S32 i = 0; i < g_frames_in_flight; i++)
				{
					D3D12_BARRIER_GROUP& group = runtime_graph.barrier_groups[i][write_index];
					group.Type = D3D12_BARRIER_TYPE_TEXTURE;
					group.NumBarriers = texture_barrier_count;
					group.pTextureBarriers = &runtime_graph.texture_barriers[i][texture_barrier_start_index];
				}
				barrier_group_count++;
			}

			S32 const used_pass_index = pass_index++;

			RenderGraphPass& runtime_pass = runtime_graph.passes[used_pass_index];
			runtime_pass.executor = pass->executor;
			runtime_pass.inputs_start_index = input_start_index;
			runtime_pass.input_count = input_count;
			runtime_pass.outputs_start_index = output_start_index;
			runtime_pass.output_count = output_count;
			runtime_pass.barrier_groups_start_index = barrier_groups_start_index;
			runtime_pass.barrier_group_count = barrier_group_count;
			runtime_pass.color_target_start_index = color_target_start_index;
			runtime_pass.color_target_count = color_target_count;
			runtime_pass.depth_target_index = depth_target_start_index;
			runtime_pass.clear_color_start_index = clear_color_start_index;
			runtime_pass.clear_color_count = clear_color_index - clear_color_start_index;
			runtime_pass.clear_depth_start_index = clear_depth_start_index;
			runtime_pass.clear_depth_count = clear_depth_index - clear_depth_start_index;
			runtime_pass.discard_start_index = discard_start_index;
			runtime_pass.discard_count = discard_index - discard_start_index;

			RuntimeGraphPassDebugData& runtime_pass_debug_data = runtime_graph.passes_debug_data[used_pass_index];
			runtime_pass_debug_data.name = pass->name;
		}
	}

	// PAW_LOG_INFO("============================================================================================");
	// PAW_LOG_INFO("============================================================================================");
	// PAW_LOG_INFO("============================================================================================");

	for (S32 pass_index = 0; pass_index < runtime_graph.passes.count; pass_index++)
	{
		RenderGraphPass const& pass = runtime_graph.passes[pass_index];
		// RuntimeGraphPassDebugData_t const& pass_debug_data = runtime_graph.passes_debug_data[pass_index];
		//  PAW_LOG_INFO("Pass: %p{str}", &pass_debug_data.name);
		Slice<D3D12_BARRIER_GROUP const> const barrier_groups = ConstSubSlice(runtime_graph.barrier_groups.row(0), pass.barrier_groups_start_index, pass.barrier_group_count);
		for (D3D12_BARRIER_GROUP const barrier_group : barrier_groups)
		{
			for (S32 barrier_index = 0; barrier_index < static_cast<S32>(barrier_group.NumBarriers); barrier_index++)
			{
				D3D12_TEXTURE_BARRIER const& barrier = barrier_group.pTextureBarriers[barrier_index];
				static byte buffer[512];
				UINT name_length = PAW_ARRAY_COUNT(buffer);
				barrier.pResource->GetPrivateData(WKPDID_D3DDebugObjectName, &name_length, buffer);

				// PAW_LOG_INFO("Barrier for %.*s:\n\tSync: %s to %s\n\tAccess: %s to %s\n\tLayout: %s to %s", name_length, buffer, get_d3d12_sync_name(barrier.SyncBefore), get_d3d12_sync_name(barrier.SyncAfter), get_d3d12_access_name(barrier.AccessBefore), get_d3d12_access_name(barrier.AccessAfter), get_d3d12_layour_name(barrier.LayoutBefore), get_d3d12_layour_name(barrier.LayoutAfter));
			}
		}

		// PAW_LOG_INFO("============================================================================================");
	}

	runtime_graph.final_resources = final_resources;
	S32 id = 0;
	S32 const height_per_pass = 60;
	{
		char buffer[2048];
		S32 walker = 0;

		S32 const width_per_pass = 250;
		S32 x = 0;
		S32 y = 0;
		for (S32 pass_index = 0; pass_index < runtime_graph.passes_debug_data.count; pass_index++)
		{
			RuntimeGraphPassDebugData const& pass = runtime_graph.passes_debug_data[pass_index];
			// {"id":"0","type":"text","text":"Scene View","x":-700,"y":-40,"width":360,"height":60},
			walker += std::snprintf(buffer + walker, 2048, "\t\t{\"id\":\"%d\",\"type\":\"text\",\"text\":\"" PAW_STR_FMT "\",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}%s\n", id, PAW_FMT_STR(pass.name), x, y, width_per_pass, height_per_pass, ",");
			x += width_per_pass;
			id++;
		}

		x = 0;
		y += height_per_pass;
		for (S32 resource_index = 0; resource_index < runtime_graph.resources_debug_data.count; resource_index++)
		{
			RuntimeGraphResourceDebugData_t const& resource = runtime_graph.resources_debug_data[resource_index];
			S32 const span_pass_count = (resource.end_pass_index + 1) - resource.start_pass_index;
			x = width_per_pass * resource.start_pass_index;
			S32 const width = span_pass_count * width_per_pass;
			// {"id":"0","type":"text","text":"Scene View","x":-700,"y":-40,"width":360,"height":60},
			walker += std::snprintf(buffer + walker, 2048, "\t\t{\"id\":\"%d\",\"type\":\"text\",\"text\":\"" PAW_STR_FMT "\",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}%s\n", id, PAW_FMT_STR(resource.name), x, y, width, height_per_pass, resource_index < runtime_graph.resources_debug_data.count - 1 ? "," : "");
			y += height_per_pass;
			id++;
		}

		std::fprintf(stdout, "%s\n\n", buffer);
	}

	{
		char buffer[2048];
		S32 walker = 0;
		F32 available_x = 800;
		F32 x_offset = 0;
		F32 y_offset = 400;

		for (RuntimeGraphResourceDebugData_t const& resource : runtime_graph.resources_debug_data)
		{
			F32 const pixels_per_heap_byte = available_x / F32(runtime_graph.total_frame_heap_size_bytes);
			F32 const heap_start_x = x_offset;
			F32 const width = pixels_per_heap_byte * resource.size_bytes;
			F32 const offset = pixels_per_heap_byte * resource.offset_bytes;
			walker += std::snprintf(buffer + walker, 2048, "\t\t{\"id\":\"%d\",\"type\":\"text\",\"text\":\"" PAW_STR_FMT "\",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}%s\n", id, PAW_FMT_STR(resource.name), S32(heap_start_x + offset), S32(y_offset), S32(width), S32(height_per_pass), ",");

			// ImGui::SetCursorPos(ImVec2(heap_start_x + offset, y_offset));
			// ImGui::Button(resource.name.ptr, ImVec2(width, lifetime_height));
			// if (ImGui::IsItemHovered())
			{
				// ImGui::SetTooltip("%.*s", int(resource.name.size_bytes), resource.name.ptr);
			}

			y_offset += height_per_pass;
			id++;
		}
		std::fprintf(stdout, "%s\n\n", buffer);
	}

	{
		char buffer[2048];
		S32 walker = 0;
		// for (S32 pass_index = 0; pass_index < runtime_graph.passes_debug_data.count; pass_index++)
		//{
		//	RuntimeGraphPassDebugData const& pass = runtime_graph.passes_debug_data[pass_index];
		//	walker += std::snprintf(buffer + walker, 2048, "\tP%d[" PAW_STR_FMT "]\n", pass_index, PAW_FMT_STR(pass.name));
		// }

		// for (S32 resource_index = 0; resource_index < runtime_graph.resources_debug_data.count; resource_index++)
		//{
		//	RuntimeGraphResourceDebugData_t const& resource = runtime_graph.resources_debug_data[resource_index];
		//	walker += std::snprintf(buffer + walker, 2048, "\tR%d{" PAW_STR_FMT "}\n", resource_index, PAW_FMT_STR(resource.name));
		// }

		for (Gfx::GraphPass* pass = first_pass; pass != nullptr; pass = pass->next_pass)
		{
			for (ResourceInstanceRef* read_ref = pass->first_read_ref; read_ref; read_ref = read_ref->next)
			{
				Resource const* const resource = read_ref->instance->resource;
				walker += std::snprintf(buffer + walker, 2048, "\tR%d{" PAW_STR_FMT "} -->|Read| P%d[" PAW_STR_FMT "]\n", resource->index, PAW_FMT_STR(resource->name), pass->index, PAW_FMT_STR(pass->name));
			}

			for (ResourceInstanceRef* write_ref = pass->first_write_ref; write_ref; write_ref = write_ref->next)
			{
				Resource const* const resource = write_ref->instance->resource;
				walker += std::snprintf(buffer + walker, 2048, "\tP%d[" PAW_STR_FMT "] -->|Write| R%d{" PAW_STR_FMT "}\n", pass->index, PAW_FMT_STR(pass->name), resource->index, PAW_FMT_STR(resource->name));
			}
		}
		/*for (S32 pass_index = 0; pass_index < runtime_graph.passes_debug_data.count; pass_index++)
		{
			RuntimeGraphPass const& pass = runtime_graph.passes[pass_index];

		}*/

		std::fprintf(stdout, "%s", buffer);
	}
	return &runtime_graph;
}

IAllocator* Gfx::GraphBuilder::GetGraphAllocator()
{
	return final_graph_allocator;
}

void RenderGraph(Gfx::State& state, Gfx::Graph* graph, Gfx::CommandList command_list_handle)
{
	graph->Render(command_list_handle, state);
}

Gfx::TextureDescriptor RenderGraphGetTextureDescriptor(Gfx::Graph* graph, Gfx::GraphTexture texture)
{
	return {graph->GetSrv(texture).value};
}
