#pragma once

#include <core/std.h>
#include <core/memory_types.h>
#include <core/string_types.h>
#include <core/gfx_types.h>

#include <core/memory.inl>

#include <tuple>

namespace Gfx
{

	State& Init(IAllocator* allocator, IAllocator* debug_allocator);
	void Render(State& state);
	void Deinit(State& state);

	enum class Format
	{
		R16G16B16A16_Float,
		R8G8B8A8_Unorm,
		R10G10B10A2_Unorm,
		R32_Float,
		Depth32_Float,
		Count,
	};

	enum class Lifetime
	{
		Dynamic,
		Static,
		Temporary,
	};

	struct BufferDescriptor
	{
		U32 value;
	};

	struct TextureDescriptor
	{
		U32 value;
	};

	struct SamplerDescriptor
	{
		U32 value;
	};

	struct RenderTargetDescriptor
	{
		U32 value;
	};

	struct DepthTargetDescriptor
	{
		U32 value;
	};

	struct Texture
	{
		S32 index;
		U32 generation;
	};

	struct TextureDesc
	{
		S32 width = 0;
		S32 height = 0;
		Format format = Format::R8G8B8A8_Unorm;
		Lifetime lifetme = Lifetime::Dynamic;
		StringView8 debug_name = PAW_STR("Unknown Texture");
		MemorySlice data{};
	};

	Texture CreateTexture(State&, TextureDesc&& desc);
	TextureDescriptor GetTextureDescriptor(State& state, Texture texture);

	struct Sampler
	{
		S32 index;
		U32 generation;
	};

	enum class Filter
	{
		Nearest,
		Count,
	};

	enum class AddressMode
	{
		Clamp,
		Count,
	};

	struct SamplerDesc
	{
		Filter filter = Filter::Nearest;
		AddressMode address_u = AddressMode::Clamp;
		AddressMode address_v = AddressMode::Clamp;
		AddressMode address_w = AddressMode::Clamp;
	};

	Sampler CreateSampler(State& state, SamplerDesc&& desc);
	SamplerDescriptor GetSamplerDescriptor(State& state, Sampler sampler);

	struct Buffer
	{
		S32 index;
		U32 generation;
	};

	struct BufferDesc
	{
	};

	Buffer CreateBuffer(State&, BufferDesc&& desc);

	struct Pipeline
	{
		S32 index;
		U32 generation;
	};

	enum class ColorMask : U8
	{
		Red = 1 << 0,
		Green = 1 << 1,
		Blue = 1 << 2,
		Alpha = 1 << 3,
		All = Red | Green | Blue | Alpha,
	};

	static constexpr inline ColorMask operator|(ColorMask lhs, ColorMask rhs)
	{
		return ColorMask(U8(lhs) | U8(rhs));
	}

	enum class Blend
	{
		Zero,
		One,
		SrcColor,
		InvSrcColor,
		SrcAlpha,
		InvSrcAlpha,
		Count,
	};

	enum class BlendOp
	{
		Add,
		Subtract,
		Count,
	};

	struct BlendState
	{
		bool enabled = false;
		ColorMask write_mask = ColorMask::All;
		Blend color_src;
		Blend color_dest;
		BlendOp color_op;
		Blend alpha_src;
		Blend alpha_dest;
		BlendOp alpha_op;
	};

	enum class Topology
	{
		Triangles,
		Count,
	};

	enum class FillMode
	{
		Wireframe,
		Solid,
		Count,
	};

	enum class CullMode
	{
		None,
		Front,
		Back,
		Count,
	};

	struct RasterizerState
	{
		FillMode fill_mode = FillMode::Solid;
		CullMode cull_mode = CullMode::Back;
	};

	struct GraphicsPipelineDesc
	{
		StringView8 debug_name = PAW_STR("Unknown Pipeline");
		Slice<BlendState const> blend_states = {{}};
		Slice<Format const> render_target_formats = {Format::R8G8B8A8_Unorm};
		Format depth_stencil_format = Format::Depth32_Float;
		RasterizerState rasterizer_state{};
		U32 sampler_mask = 0xFFFFFFFF;
		U32 sample_count = 1;
		Topology topology = Topology::Triangles;
		MemorySlice vertex_mem{};
		MemorySlice pixel_mem{};
	};

	Pipeline CreateGraphicsPipeline(State&, GraphicsPipelineDesc&& desc);

	struct CommandList
	{
		S32 index;
		U32 generation;
	};
	void BindPipeline(State& state, CommandList command_list, Pipeline pipeline);

	void SetConstants(State& state, CommandList command_list, void const* constants, PtrSize size_bytes);

	void SetTopology(State& state, CommandList command_list, Topology topology);

	template <typename T>
	void SetConstants(State& state, CommandList command_list, T const& constants)
	{
		SetConstants(state, command_list, &constants, sizeof(T));
	}

	void Draw(State& state, CommandList command_list, U32 vert_count, U32 instance_count, U32 start_vertex, U32 start_instance);

	union ClearValue
	{
		F32 color[4];
		struct
		{
			F32 depth;
			U8 stencil;
		} depth_stencil;
	};

	enum class Access
	{
		None,
		RenderTarget,
		VertexShader,
		PixelShader,
		Depth,
		Stencil,
		Present,
		Count,
	};

	struct AccessFlags
	{
		U32 flags;

		bool contains(Access access) const
		{
			return (flags & (1 << U32(access))) > 0;
		}

		void operator=(Access rhs)
		{
			flags = 1 << U32(rhs);
		}
	};

	static constexpr inline AccessFlags operator|(Access lhs, Access rhs)
	{
		return AccessFlags((1 << U32(lhs)) | (1 << U32(rhs)));
	}

	static void operator|=(AccessFlags& lhs, Access rhs)
	{
		lhs.flags |= 1 << U32(rhs);
	}

	struct Graph;
	class GraphBuilder;

	enum class InitialState
	{
		Undefined,
		Clear,
	};

	struct GraphTextureDesc
	{
		Access access = Access::None;
		S32 width;
		S32 height;
		Format format;
		StringView8 name = PAW_STR("Unknown Texture");
		InitialState initial_state = InitialState::Undefined;
		ClearValue clear_value;
		S32 sample_count = 1;
	};

	struct GraphPass;
	struct GraphTexture
	{
		U64 handle;
	};

	GraphBuilder& CreateGraphBuilder(Gfx::State& state, IAllocator* allocator);
	void DestroyGraphBuilder(GraphBuilder& builder);

	void BuildGraph(Gfx::State& state, GraphBuilder& builder);

	GraphPass& GraphCreatePass(GraphBuilder& builder, StringView8 name);
	GraphTexture GraphCreateTexture(GraphBuilder& builder, GraphPass& pass, GraphTextureDesc&& desc);
	GraphTexture GraphReadTexture(GraphBuilder& builder, GraphPass& pass, GraphTexture texture, Gfx::Access access);
	GraphTexture GraphWriteTexture(GraphBuilder& builder, GraphPass& pass, GraphTexture texture, Gfx::Access access);

	GraphTexture GraphGetBackBuffer(GraphBuilder& builder);

	IAllocator* GetGraphAllocator(Gfx::GraphBuilder&);

	Gfx::TextureDescriptor GetGraphTextureDescriptor(Gfx::State& state, Gfx::GraphTexture texture);

	struct GraphExecutor
	{
		virtual void Execute(State&, CommandList) = 0;
	};

	void GraphSetExecutorPtr(GraphBuilder& builder, GraphPass& pass, GraphExecutor* executor);

	template <typename T>
	T& GraphSetExecutor(GraphBuilder& builder, GraphPass& pass)
	{
		T* executor = PAW_NEW_IN(GetGraphAllocator(builder), T)();
		GraphSetExecutorPtr(builder, pass, executor);
		return *executor;
	}

	template <typename T>
	struct BufferAlloc
	{
		T* ptr;
		BufferDescriptor descriptor;
		U32 offset_bytes;
	};

	template <typename T>
	struct BufferSliceAlloc
	{
		Slice<T> items;
		BufferDescriptor descriptor;
		U32 offset_bytes;
	};

	struct RawBufferAlloc
	{
		Byte* ptr;
		BufferDescriptor descriptor;
		U32 offset_bytes;
	};

	RawBufferAlloc AllocTempBuffer(State& state, PtrSize size_bytes, PtrSize align_bytes);

	template <typename T>
	BufferAlloc<T> AllocTempBuffer(State& state)
	{
		RawBufferAlloc const alloc = AllocTempBuffer(state, sizeof(T), alignof(T));
		// #TODO: Maybe I should placement new here? Would mean an extra write into host visible memory, does that cost more?
		return {reinterpret_cast<T*>(alloc.ptr), alloc.descriptor, alloc.offset_bytes};
	}

	template <typename T>
	BufferSliceAlloc<T> AllocTempBuffer(State& state, S32 count)
	{
		RawBufferAlloc const alloc = AllocTempBuffer(state, sizeof(T) * count, alignof(T));
		// #TODO: Maybe I should placement new here? Would mean an extra write into host visible memory, does that cost more?
		return {{reinterpret_cast<T*>(alloc.ptr), count}, alloc.descriptor, alloc.offset_bytes};
	}
}