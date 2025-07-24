#include <core/gfx.h>
#include <core/assert.h>
#include <core/arena.h>
#include <core/memory_types.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-enum-value"
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wunused-value"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#include <d3d12/d3d12.h>
#include <dxgi1_6.h>
#pragma clang diagnostic pop

inline static constexpr S32 g_frames_in_flight = 2;

#ifdef __INTELLISENSE__
#define PAW_SUPPRESS_UBSAN
#else
#define PAW_SUPPRESS_UBSAN __attribute__((no_sanitize("undefined")))
#endif

template <typename T>
void** PAW_SUPPRESS_UBSAN _IID_PPV_ARGS_Helper(T** pp)
{
	// #pragma prefast(suppress : 6269, "Tool issue with unused static_cast")
	(void)static_cast<IUnknown*>(*pp); // make sure everyone derives from IUnknown
	return reinterpret_cast<void**>(pp);
}
// -Wlanguage-extension-token
// Use this to supress the ubsan error for the cast to IUnknown
#define PAW_IID_PPV_ARGS(ppType) __uuidof(**(ppType)), IID_PPV_ARGS_Helper(ppType)

#define DX_VERIFY(code)                        \
	do                                         \
	{                                          \
		HRESULT _result = code;                \
		PAW_ASSERT(SUCCEEDED(_result), #code); \
	} while (false);

static inline void SetDebugName(ID3D12Resource* resource, StringView8 const& name)
{
	resource->SetPrivateData(WKPDID_D3DDebugObjectName, UINT(name.size_bytes), name.ptr);
}

template <typename T>
struct Handle
{
	S32 index;
	U32 generation;
};

template <typename T>
class FixedSizePool : NonCopyable
{
public:
	union Slot
	{
		T data;
		S32 next_free_index;
	};

	static constexpr S32 null_slot = -1;

	FixedSizePool() = default;

	void Init(IAllocator* allocator, S32 count)
	{
		slots = PAW_NEW_SLICE_IN(allocator, count, Slot);
		generations = PAW_NEW_SLICE_IN(allocator, count, U32);

		for (S32 i = 0; i < slots.count - 1; ++i)
		{
			Slot& slot = slots[i];
			new (&slot.next_free_index) S32(i + 1);
		}
		new (&slots[slots.count - 1].next_free_index) S32(-1);
	}

	FixedSizePool(IAllocator* allocator, S32 count)
	{
		Init(allocator, count);
	}

	~FixedSizePool()
	{
		PAW_DELETE_SLICE(slots);
		PAW_DELETE_SLICE(generations);
	}

	void CheckHandle(Handle<T> handle)
	{
		U32 const generation = generations[handle.index];
		PAW_ASSERT(handle.generation == generation, "Generations do not match");
		PAW_ASSERT(handle.index != null_slot, "Index is null");
		PAW_ASSERT(handle.index < slots.count, "Invalid index");
		PAW_ASSERT(handle.index >= 0, "Invalid index");
	}

	Handle<T> Alloc()
	{
		PAW_ASSERT(first_free_index != null_slot, "No free slots");
		S32 const index = first_free_index;
		Slot& free_slot = slots[index];
		first_free_index = free_slot.next_free_index;

		new (&free_slot.data) T();
		U32 const generation = generations[index];
		return {index, generation};
	}

	void Free(Handle<T> handle)
	{
		CheckHandle(handle);
		Slot& slot = slots[handle.index];
		new (&slot.next_free_index) S32(first_free_index);
		first_free_index = handle.index;
		generations[handle.index]++;
	}

	T& GetValue(Handle<T> handle)
	{
		CheckHandle(handle);
		return slots[handle.index].data;
	}

private:
	Slice<Slot> slots;
	Slice<U32> generations;
	S32 first_free_index = 0;
};

enum class DescriptorType
{
	CbvSrvUav,
	Sampler,
	Rtv,
	Dsv,
};

template <DescriptorType Type>
struct DescriptorIndex
{
	U32 value;
};

inline static constexpr DescriptorIndex<DescriptorType::CbvSrvUav> g_null_cbv_srv_uav_descriptor_index{U32(-1)};
inline static constexpr DescriptorIndex<DescriptorType::Sampler> g_null_sampler_descriptor_index{U32(-1)};
inline static constexpr DescriptorIndex<DescriptorType::Rtv> g_null_rtv_descriptor_index{U32(-1)};
inline static constexpr DescriptorIndex<DescriptorType::Dsv> g_null_dsv_descriptor_index{U32(-1)};

template <DescriptorType Type>
static inline bool operator==(DescriptorIndex<Type> a, DescriptorIndex<Type> b)
{
	return a.value == b.value;
}

class DescriptorPool : NonCopyable
{
public:
	DescriptorPool() = default;

	void Init(ID3D12Device12* device, IAllocator* allocator);

	DescriptorIndex<DescriptorType::CbvSrvUav> AllocCbvSrvUav();

	void FreeCbvSrvUav(DescriptorIndex<DescriptorType::CbvSrvUav> index);

	D3D12_CPU_DESCRIPTOR_HANDLE GetCbvSrvUavCPUHandle(DescriptorIndex<DescriptorType::CbvSrvUav> index);

	D3D12_GPU_DESCRIPTOR_HANDLE GetCbvSrvUavGPUHandle(DescriptorIndex<DescriptorType::CbvSrvUav> index);

	ID3D12DescriptorHeap* GetCbvSrvUavHeap();

	DescriptorIndex<DescriptorType::Rtv> AllocRtv();

	void FreeRtv(DescriptorIndex<DescriptorType::Rtv> index);

	D3D12_CPU_DESCRIPTOR_HANDLE GetRtvCPUHandle(DescriptorIndex<DescriptorType::Rtv> index);

	D3D12_GPU_DESCRIPTOR_HANDLE GetRtvGPUHandle(DescriptorIndex<DescriptorType::Rtv> index);

	ID3D12DescriptorHeap* GetRtvHeap();

	DescriptorIndex<DescriptorType::Dsv> AllocDsv();

	void FreeDsv(DescriptorIndex<DescriptorType::Dsv> index);

	D3D12_CPU_DESCRIPTOR_HANDLE GetDsvCPUHandle(DescriptorIndex<DescriptorType::Dsv> index);

	D3D12_GPU_DESCRIPTOR_HANDLE GetDsvGPUHandle(DescriptorIndex<DescriptorType::Dsv> index);

	ID3D12DescriptorHeap* GetDsvHeap();

	DescriptorIndex<DescriptorType::Sampler> AllocSampler();

	void FreeSampler(DescriptorIndex<DescriptorType::Sampler> index);

	D3D12_CPU_DESCRIPTOR_HANDLE GetSamplerCPUHandle(DescriptorIndex<DescriptorType::Sampler> index);

	D3D12_GPU_DESCRIPTOR_HANDLE GetSamplerGPUHandle(DescriptorIndex<DescriptorType::Sampler> index);

	ID3D12DescriptorHeap* GetSamplerHeap();

private:
	template <DescriptorType Type>
	class Pool
	{
	public:
		void Init(ID3D12Device12* device, IAllocator* allocator, D3D12_DESCRIPTOR_HEAP_TYPE in_type, D3D12_DESCRIPTOR_HEAP_FLAGS in_flags, U32 count)
		{
			const D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc{
				.Type = in_type,
				.NumDescriptors = count,
				.Flags = in_flags,
			};

			DX_VERIFY(device->CreateDescriptorHeap(&descriptor_heap_desc, PAW_IID_PPV_ARGS(&heap)));

			start_cpu_handle = heap->GetCPUDescriptorHandleForHeapStart();

			if ((in_flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
			{
				start_gpu_handle = heap->GetGPUDescriptorHandleForHeapStart();
			}
			handle_increment_size = device->GetDescriptorHandleIncrementSize(descriptor_heap_desc.Type);

			free_handles = PAW_NEW_SLICE_IN(allocator, count, U32);
			for (U32 i = 0; i < count; i++)
			{
				// Put them in reverse order so we use the lower numbers first
				free_handles[i] = count - i - 1;
			}

			free_handle_count = free_handles.count;
			type = in_type;
			flags = in_flags;
		}

		~Pool()
		{
			PAW_DELETE_SLICE(free_handles);
			heap->Release();
		}

		DescriptorIndex<Type> Alloc()
		{
			PAW_ASSERT(free_handle_count > 0, "Run out of handles");
			free_handle_count--;
			U32 const free_handle = free_handles[free_handle_count];
			free_handles[free_handle_count] = 0xDEADBEEF;
			return {free_handle};
		}

		void Free(DescriptorIndex<Type> index)
		{
			PAW_ASSERT(free_handle_count < free_handles.count, "Too many free handles");
			PAW_ASSERT(free_handles[free_handle_count] == 0xDEADBEEF, "Freeing a handle that has already been freed before");
			free_handles[free_handle_count] = index.value;
			free_handle_count++;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE GetCPU(DescriptorIndex<Type> index)
		{
			return D3D12_CPU_DESCRIPTOR_HANDLE{start_cpu_handle.ptr + index.value * handle_increment_size};
		}

		D3D12_GPU_DESCRIPTOR_HANDLE GetGPU(DescriptorIndex<Type> index)
		{
			PAW_ASSERT((flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, "Incorrect heap type for gpu access");
			return D3D12_GPU_DESCRIPTOR_HANDLE{start_gpu_handle.ptr + index.value * handle_increment_size};
		}

		ID3D12DescriptorHeap* GetHeap()
		{
			return heap;
		}

	private:
		ID3D12DescriptorHeap* heap = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE start_cpu_handle;
		D3D12_GPU_DESCRIPTOR_HANDLE start_gpu_handle;
		U32 handle_increment_size;
		Slice<U32> free_handles;
		S32 free_handle_count;
		D3D12_DESCRIPTOR_HEAP_TYPE type;
		D3D12_DESCRIPTOR_HEAP_FLAGS flags;
	};

	Pool<DescriptorType::CbvSrvUav> cbv_srv_uav_descriptor_pool;
	Pool<DescriptorType::Sampler> sampler_descriptor_pool;
	Pool<DescriptorType::Rtv> rtv_descriptor_pool;
	Pool<DescriptorType::Dsv> dsv_descriptor_pool;
};

class CommandListPool : NonCopyable
{
public:
	struct Slot
	{
		ID3D12CommandList* command_list;
		PtrSize next_free_index;
	};

	static constexpr S32 null_slot = (S32)-1;

	void init(ID3D12Device12* device, D3D12_COMMAND_LIST_TYPE type, IAllocator* allocator, PtrSize count);

	~CommandListPool();

	Gfx::CommandList alloc(ID3D12CommandList*& out_command_list);

	Gfx::CommandList allocGraphics(ID3D12GraphicsCommandList9*& out_command_list);

	void free(Gfx::CommandList index);

	ID3D12GraphicsCommandList9* GetGraphicsCommandList(Gfx::CommandList index);

	ID3D12CommandList* GetCommandList(Gfx::CommandList index);

private:
	Slice<Slot> slots;
	S32 first_free_index;
};

enum QueueType : S32
{
	QueueType_Graphics,
	QueueType_Compute,
	QueueType_Copy,
	QueueType_Count,
};

class CommandListAllocator : NonCopyable
{
public:
	CommandListAllocator() = default;

	void Init(ID3D12Device12* device, IAllocator* allocator);
	~CommandListAllocator();

	ID3D12CommandQueue* GetPresentQueue();
	void Reset(S32 local_frame_index);
	Gfx::CommandList GrabAndResetGraphicsCommandList(S32 local_frame_index);
	void CloseExecuteAndFreeCommandList(Gfx::CommandList handle);
	ID3D12GraphicsCommandList9* GetGraphicsCommandList(Gfx::CommandList handle);

private:
	ID3D12CommandQueue* command_queues[QueueType_Count];
	ID3D12Fence* queue_fences[QueueType_Count];
	ID3D12CommandAllocator* command_allocators[QueueType_Count][g_frames_in_flight];
	CommandListPool command_list_pools[QueueType_Count];
};

class StaticTexturePool : NonCopyable
{
public:
	struct SlotData
	{
		ID3D12Resource* resource;
		DescriptorIndex<DescriptorType::CbvSrvUav> srv_index;
	};

	union Slot
	{
		SlotData data;
		S32 next_free_index;
	};

	static constexpr S32 null_slot = -1;

	StaticTexturePool() = default;
	~StaticTexturePool();

	void Init(IAllocator* allocator, Gfx::State& state);

	Gfx::Texture Alloc(Gfx::TextureDesc&& desc, Gfx::State& state);
	void Free(Gfx::Texture handle);
	DescriptorIndex<DescriptorType::CbvSrvUav> GetSrvDescriptor(Gfx::Texture handle);

private:
	Slice<Slot> slots;
	Slice<U32> generations;
	S32 first_free_index = 0;

	static constexpr PtrSize heap_size_bytes = MegaBytes(128);
	ID3D12Heap* heap = nullptr;
	FixedSizeArenaAllocator allocator{};
};

template <typename T>
struct BufferAlloc
{
	T* ptr;
	DescriptorIndex<DescriptorType::CbvSrvUav> descriptor_index;
	U32 offset_bytes;
};

class TransientAllocator
{
public:
	void Init(Gfx::State& state);

	void Reset()
	{
		allocator.FreeAll();
	}

	Gfx::RawBufferAlloc Alloc(PtrSize size_bytes, PtrSize align_bytes);

private:
	static constexpr D3D12_HEAP_PROPERTIES const heap_props{
		.Type = D3D12_HEAP_TYPE_UPLOAD,
		.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
		.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
		.CreationNodeMask = 1,
		.VisibleNodeMask = 1,
	};

	static constexpr PtrSize size_bytes = MegaBytes(4);

	DescriptorIndex<DescriptorType::CbvSrvUav> descriptor_index{};
	ID3D12Resource* resource{};
	FixedSizeArenaAllocator allocator{};
};

struct PipelineStateObject
{
	ID3D12PipelineState* pso;
	ID3D12RootSignature* root_signature;
};

struct Gfx::State : NonCopyable
{
	ID3D12Device12* device = nullptr;
	DescriptorPool descriptor_pool;
	StaticTexturePool static_texture_pool;
	CommandListAllocator command_list_allocator;
	FixedSizePool<PipelineStateObject> pipeline_pool;
	FixedSizePool<SamplerDescriptor> sampler_pool;

	ID3D12CommandQueue* present_command_queue = nullptr;
	HANDLE present_fence_event;
	ID3D12Fence* present_fence = nullptr;
	U64 fence_values[g_frames_in_flight]{};
	S32 local_frame_index;
	U64 last_frames_fence_value = 0;

	ID3D12Resource* backbuffer_resources[g_frames_in_flight];
	IDXGISwapChain3* swapchain = nullptr;
	HANDLE swapchain_event;
	Gfx::Format swapchain_format;

	TransientAllocator frame_buffer_allocators[g_frames_in_flight];

	ArenaAllocator graph_allocator{};
	Gfx::Graph* current_graph = nullptr;

	IAllocator* debug_allocator = nullptr;

	IDxcUtils* utils = nullptr;
	IDxcCompiler3* compiler = nullptr;
	IDxcIncludeHandler* include_handler = nullptr;

	State() = default;
};

static constexpr PtrSize g_texture_format_sizes_bytes[(int)Gfx::Format::Count]{
	/* R16G16B16A16_Float */ 8,
	/* R8G8B8A8_Unorm */ 4,
	/* R10G10B10A2_Unorm */ 4,
	/* R32_Float */ 4,
	/* Depth32_Float */ 4,
};

static constexpr DXGI_FORMAT g_texture_format_map[(int)Gfx::Format::Count]{
	/* R16G16B16A16_Float */ DXGI_FORMAT_R16G16B16A16_FLOAT,
	/* R8G8B8A8_Unorm */ DXGI_FORMAT_R8G8B8A8_UNORM,
	/* R10G10B10A2_Unorm */ DXGI_FORMAT_R10G10B10A2_UNORM,
	/* R32_Float */ DXGI_FORMAT_R32_FLOAT,
	/* Depth32_Float */ DXGI_FORMAT_D32_FLOAT,
};

static constexpr DXGI_FORMAT g_srv_format_map[int(Gfx::Format::Count)]{
	/* R16G16B16A16_Float */ DXGI_FORMAT_R16G16B16A16_FLOAT,
	/* R8G8B8A8_Unorm */ DXGI_FORMAT_R8G8B8A8_UNORM,
	/* R10G10B10A2_Unorm */ DXGI_FORMAT_R10G10B10A2_UNORM,
	/* R32_Float */ DXGI_FORMAT_R32_FLOAT,
	/* Depth32_Float */ DXGI_FORMAT_R32_FLOAT,
};

static constexpr D3D12_PRIMITIVE_TOPOLOGY g_topologies[int(Gfx::Topology::Count)]{
	/* Triangles */ D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
};

static constexpr D3D12_PRIMITIVE_TOPOLOGY_TYPE g_topology_types[int(Gfx::Topology::Count)]{
	/* Triangles */ D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
};

static constexpr D3D12_FILTER g_filters[int(Gfx::Filter::Count)]{
	/* Nearest */ D3D12_FILTER_MIN_MAG_MIP_POINT,
};

static constexpr D3D12_TEXTURE_ADDRESS_MODE g_address_modes[int(Gfx::AddressMode::Count)]{
	/* Clamp */ D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
};

static constexpr D3D12_FILL_MODE g_fill_modes[S32(Gfx::FillMode::Count)]{
	/* Wireframe */ D3D12_FILL_MODE_WIREFRAME,
	/* Solid */ D3D12_FILL_MODE_SOLID,
};

static constexpr D3D12_CULL_MODE g_cull_modes[S32(Gfx::CullMode::Count)]{
	/* None */ D3D12_CULL_MODE_NONE,
	/* Front */ D3D12_CULL_MODE_FRONT,
	/* Back */ D3D12_CULL_MODE_BACK,
};