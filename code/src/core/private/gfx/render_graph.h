#pragma once

#include <core/gfx.h>

enum class ResourceOrigin
{
	Graph,
	Import,
};

struct Resource
{
	StringView8 name;
	S32 width;
	S32 height;
	Gfx::Format format;
	S32 sample_count;
	S32 ref_count;
	S32 index;
	Gfx::AccessFlags all_accesses;
	Gfx::Access first_access;
	Gfx::Access last_access;

	Gfx::InitialState initial_state;
	Gfx::ClearValue clear_value;

	ResourceOrigin origin = ResourceOrigin::Graph;

	bool NeedsClearValue() const
	{
		return initial_state == Gfx::InitialState::Clear && (all_accesses.contains(Gfx::Access::RenderTarget) || all_accesses.contains(Gfx::Access::Depth));
	}
};

struct ResourceInstance;

class Gfx::GraphBuilder
{
public:
	GraphBuilder(GraphTextureDesc in_backbuffer, IAllocator* allocator, IAllocator* final_graph_allocator);

	GraphTexture GetBackBuffer();
	GraphTexture CreateTexture(GraphPass& pass, GraphTextureDesc&& desc);
	GraphPass& AddPass(StringView8 name);
	GraphTexture ReadTexture(GraphPass& pass, GraphTexture handle, Access access);
	GraphTexture WriteTexture(GraphPass& pass, GraphTexture handle, Access access);
	void SetExecutor(GraphPass& pass, GraphExecutor* executor);

	Graph* Build(State& gfx_state);

	IAllocator* GetGraphAllocator();

private:
	static void ProcessPass(GraphPass* pass, Slice<S32> const& visited, Slice<S32> const& on_stack, Slice<GraphPass const*> const& final_passes, S32& final_write_index);

	IAllocator* const allocator;
	IAllocator* const final_graph_allocator;
	GraphPass* first_pass = nullptr;
	GraphPass* current_pass = nullptr;
	S32 pass_count = 0;
	S32 resource_count = 0;
	S32 external_resource_count = 0;
	Resource backbuffer;
	ResourceInstance* backbuffer_instance;
};

void RenderGraph(Gfx::State& state, Gfx::Graph* graph, Gfx::CommandList command_list_handle);
Gfx::TextureDescriptor RenderGraphGetTextureDescriptor(Gfx::Graph* graph, Gfx::GraphTexture texture);