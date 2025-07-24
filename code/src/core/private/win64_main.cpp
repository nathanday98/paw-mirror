#include <core/slice.inl>
#include <core/std.h>
#include <core/memory.h>
#include <core/assert.h>
#include <core/string.h>
#include <core/arena.h>
#include <core/memory.inl>
#include <core/platform.h>
#include <core/math.h>
#include <core/gfx.h>
#include <core/logger.h>
#include <cstring>

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
#include <cstdio>
#include <atomic>
#include <tuple>
#include <algorithm>
#pragma clang diagnostic pop

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

// Ported from https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
template <typename T>
class MPMCBoundedQueue
{
public:
	MPMCBoundedQueue(PtrSize buffer_size, IAllocator* allocator)
		: buffer_mask(buffer_size - 1)
	{
		PAW_ASSERT((buffer_size >= 2) && ((buffer_size & (buffer_size - 1)) == 0), "Buffer size is not a power of 2");

		buffer = PAW_NEW_SLICE_IN(allocator, buffer_size, Cell);

		for (PtrSize i = 0; i != buffer_size; i += 1)
			buffer[i].sequence.store(i, std::memory_order_relaxed);

		enqueue_pos.store(0, std::memory_order_relaxed);
		dequeue_pos.store(0, std::memory_order_relaxed);
	}

	~MPMCBoundedQueue()
	{
		PAW_DELETE_SLICE(buffer);
	}

	bool TryEnqueue(T const& data)
	{
		Cell* cell;
		PtrSize pos = enqueue_pos.load(std::memory_order_relaxed);

		for (;;)

		{
			cell = &buffer[pos & buffer_mask];
			PtrSize seq = cell->sequence.load(std::memory_order_acquire);
			intptr_t dif = (intptr_t)seq - (intptr_t)pos;
			if (dif == 0)
			{
				if (enqueue_pos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
					break;
			}
			else if (dif < 0)
				return false;
			else
				pos = enqueue_pos.load(std::memory_order_relaxed);
		}

		cell->data = data;
		cell->sequence.store(pos + 1, std::memory_order_release);

		return true;
	}

	void ForceEnqueue(T const& data)
	{
		int attempts = 0;
		while (!TryEnqueue(data))
		{
			if (++attempts > 10000)
			{
				PAW_UNREACHABLE;
			}
		}
	}

	bool TryDequeue(T& data)
	{
		Cell* cell;
		PtrSize pos = dequeue_pos.load(std::memory_order_relaxed);

		for (;;)
		{
			cell = &buffer[pos & buffer_mask];
			PtrSize seq = cell->sequence.load(std::memory_order_acquire);
			intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);

			if (dif == 0)
			{
				if (dequeue_pos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
					break;
			}
			else if (dif < 0)
				return false;
			else
				pos = dequeue_pos.load(std::memory_order_relaxed);
		}

		data = cell->data;
		cell->sequence.store(pos + buffer_mask + 1, std::memory_order_release);

		return true;
	}

	void ForceDequeue(T& data)
	{
		int attempts = 0;
		while (!TryDequeue(data))
		{
			if (++attempts > 10000)
			{
				PAW_UNREACHABLE;
			}
		}
	}

private:
	static PtrSize const cacheline_size = 64;
	typedef char CachelinePad[cacheline_size];

	struct Cell
	{
		std::atomic<PtrSize> sequence;
		T data;
	};

	CachelinePad pad0;
	Slice<Cell> buffer;
	PtrSize buffer_mask;
	CachelinePad pad1;
	std::atomic<PtrSize> enqueue_pos;
	CachelinePad pad2;
	std::atomic<PtrSize> dequeue_pos;
	CachelinePad pad3;
};

class JobQueue;

struct WorkerStartData
{
	JobQueue* job_queue;
	S32 index;
};

struct WorkerData
{
	HANDLE thread;
};

static thread_local S32 g_worker_index;

class JobQueue : NonCopyable
{
public:
	JobQueue(IAllocator* allocator)
		: queue(4096, allocator)
	{
		S32 const worker_count = GetWorkerCount();
		worker_wake_semaphore = CreateSemaphore(nullptr, 0, worker_count, nullptr);
		workers = PAW_NEW_SLICE_IN(allocator, worker_count, WorkerData);
		worker_start_data = PAW_NEW_SLICE_IN(allocator, worker_count, WorkerStartData);

		running = true;
		for (S32 i = 0; i < worker_count; ++i)
		{
			WorkerData& worker = workers[i];
			WorkerStartData& start_data = worker_start_data[i];
			start_data.job_queue = this;
			start_data.index = i;
			worker.thread = CreateThread(nullptr, 0, &WorkerLoopStub, &start_data, 0, nullptr);
		}
	}

	~JobQueue()
	{
		PAW_DELETE_SLICE(workers);
		PAW_DELETE_SLICE(worker_start_data);
	}

	void Push(JobDecl&& job)
	{
		queue.ForceEnqueue(PAW_MOVE(job));
	}

	S32 GetWorkerCount() const
	{
		return 8;
	}

private:
	static DWORD WINAPI WorkerLoopStub(LPVOID ptr)
	{
		WorkerStartData const* start_data = reinterpret_cast<WorkerStartData*>(ptr);
		start_data->job_queue->WorkerLoop(start_data->index);
		return 0;
	}

	void WorkerLoop(S32 worker_index)
	{
		PAW_INFO("Started worker %d\n", worker_index);
		g_worker_index = worker_index;
		while (running)
		{
			JobDecl job;
			while (queue.TryDequeue(job))
			{
				job.func(job.data);
			}

			WaitForSingleObject(worker_wake_semaphore, INFINITE);
		}
	}

	MPMCBoundedQueue<JobDecl> queue;
	Slice<WorkerData> workers;
	Slice<WorkerStartData> worker_start_data;
	HANDLE worker_wake_semaphore;
	std::atomic<bool> running;
};

enum class ResourceAccessType : U32
{
	ReadOnly,
	ReadWrite,
	Count,
};

struct JobResource
{
	S32 range_start;
	S32 range_end;
	ResourceAccessType access;
	S32 arg_index;
};

class JobGraph;

struct JobHandle
{
	PtrSize handle;
};

class JobBase
{
public:
	virtual ~JobBase()
	{
	}

	virtual void Execute(JobGraph& job_graph, JobHandle job) = 0;
};

class Job;

struct JobLink
{
	Job* job;
	JobLink* next_link = nullptr;
};

class Job
{
public:
	Job(StringView8 name, SrcLocation src_loc, JobBase& callable, S32 dependency_count, JobGraph& graph)
		: name(name)
		, src_location(src_loc)
		, callable(callable)
		, graph(graph)
		, parents_left_to_complete(dependency_count)
	{
	}

	void AddChildLink(JobLink* job_link)
	{
		PAW_ASSERT(attachable, "Parent is not currently attachable, are you adding to it too late?");

		// This needs to be lock free because we could be adding to an existing job
		while (true)
		{
			JobLink* old_first = first_child;
			job_link->next_link = old_first;
			if (first_child.compare_exchange_weak(old_first, job_link))
			{
				break;
			}
		}
	}

	void Execute();

	StringView8 GetName() const
	{
		return name;
	}

private:
	StringView8 const name;
	SrcLocation const src_location;
	JobBase& callable;
	JobGraph& graph;
	std::atomic<S32> parents_left_to_complete;
	std::atomic<JobLink*> first_child = nullptr;
	std::atomic<bool> attachable = true;
};

enum class JobGraphState : byte
{
	Initial, // Initialized but not inside a frame yet
	Active,	 // Inside of a frame currently
	Waiting,
	Ended, // A job has requested that the frame should end after it's finished
};

class JobGraph : NonCopyable
{
public:
	JobGraph(JobQueue& job_queue, IAllocator* persistant_allocator)
		: job_queue(job_queue)
		, allocators(PAW_NEW_SLICE_IN(persistant_allocator, job_queue.GetWorkerCount(), ArenaAllocator))
	{
		PAW_UNUSED_ARG(persistant_allocator);
	}

	~JobGraph()
	{
		PAW_DELETE_SLICE(allocators);
	}

	Job& PushJob(SrcLocation src_loc, StringView8 name, JobBase& callbable, Slice<JobHandle const> const& dependencies, Slice<JobResource const> const& resources)
	{
		PAW_UNUSED_ARG(resources);
		IAllocator* allocator = GetAllocator();
		Job* job = PAW_NEW_IN(allocator, Job)(name, src_loc, callbable, dependencies.count, *this);

		for (JobHandle parent_handle : dependencies)
		{
			Job& parent = *(Job*)parent_handle.handle;

			JobLink* job_link = PAW_NEW_IN(allocator, JobLink)();
			job_link->job = job;

			parent.AddChildLink(job_link);
		}

		return *job;
	}

	void Schedule(Job& job)
	{
		job_queue.Push({
			.name = job.GetName(),
			.data = &job,
			.func = GraphJob,
		});
	}

	IAllocator* GetAllocator()
	{
		return &allocators[g_worker_index];
	}

	JobGraphState GetState() const
	{
		return state;
	}

private:
	static void GraphJob(void* data)
	{
		Job* job = reinterpret_cast<Job*>(data);
		job->Execute();
	}

	JobQueue& job_queue;
	// This allocator lifetime matches the lifetime of a single graph execution. Jobs can use it for temporary memory
	// And it can also be used for bookkeeping that matches that lifetime
	Slice<ArenaAllocator> const allocators;
	std::atomic<JobGraphState> state = JobGraphState::Active;
};

void Job::Execute()
{
	PAW_ASSERT(graph.GetState() == JobGraphState::Active, "Job graph is not active, you should not be able to execute a job");
	PAW_ASSERT(parents_left_to_complete == 0, "Not all parents are complete");

	callable.Execute(graph, {reinterpret_cast<PtrSize>(this)});

	attachable = false;

	// S32 child_count = 0;
	//  bool switching_to_wait_job = false;
	for (JobLink* link = first_child; link != nullptr; link = link->next_link)
	{
		Job* child = link->job;

		// #TODO: I changed this because fetch_sub was triggering ubsan (it casts to unsigned and underflows)
		// This might not be sequentially consistent on non x86 platforms
		// if (child->parents_left_to_complete.fetch_sub(1, std::memory_order_seq_cst) == 1)
		if (--child->parents_left_to_complete == 0)
		{
			graph.Schedule(*child);
			// switch (child->type)
			//{
			//	case JobType::Normal:
			//	{
			//		job_queue_schedule({{.name = child->name, .data = child, .func = &graph_func}});
			//	}
			//	break;

			//	case JobType::Wait:
			//	{
			//		graph->wait_job = child;
			//		graph->frame_state = FrameState::Waiting;
			//		switching_to_wait_job = true;
			//	}
			//	break;

			//	case JobType::FrameEnd:
			//	{
			//		// #TODO: Might be pointless scheduling this, since we can't have overlapping jobs with the frame end
			//		job_queue_schedule({{.name = "FrameEnd"_str, .data = graph, .func = &frame_end_graph_func}});
			//	}
			//	break;
			//}
		}

		// child_count++;
	}
}

template <typename... Args>
using JobGraphFuncPointer = void (*)(JobGraph&, JobHandle, Args...);

template <typename... Args>
using JobGraphFuncArgs = std::tuple<JobGraph&, JobHandle, Args...>;

template <typename... Args>
class WrappedJob : public JobBase
{
public:
	WrappedJob(JobGraphFuncArgs<Args...>&& args, JobGraphFuncPointer<Args...> func)
		: args(PAW_MOVE(args))
		, ptr(func)
	{
	}

	JobGraphFuncArgs<Args...> args;
	JobGraphFuncPointer<Args...> ptr;

	virtual void Execute(JobGraph& /*job_graph*/, JobHandle job) override
	{
		// std::get<0>(args) = job_graph;
		std::get<1>(args) = job;
		std::apply(ptr, args);
	}
};

static inline constexpr JobHandle g_null_job = JobHandle{(PtrSize)-1};

template <typename... Args, typename... Inputs>
Job& JobGraphAddJob(JobGraph& graph, SrcLocation&& src, StringView8 const& name, Slice<JobHandle const> const& dependencies, JobGraphFuncPointer<Args...> func, Inputs&&... inputs)
{
	// constexpr PtrSize resource_count = (... + [&]
	//								  {
	//		JobResourceDisable<Args>::error_if_needed();
	//		if constexpr (JobResourceTypeInfo<Args>::value) {
	//			return 1;
	//		}

	//		return 0; }());

	// JobResource resources[resource_count]{};

	// PtrSize write_index = 0;
	// U32 arg_index = 0;

	//([&]
	// {

	//       using ResourceInfo = JobResourceTypeInfo<Args>;

	//       if constexpr(ResourceInfo::value) {
	//           resources[write_index++] = ResourceInfo::get(inputs, arg_index);
	//       }
	//	arg_index++; }(),
	// ...);

	WrappedJob<Args...>* job = PAW_NEW_IN(graph.GetAllocator(), WrappedJob<Args...>)(JobGraphFuncArgs<Args...>(graph, g_null_job, inputs...), func);

	return graph.PushJob(std::move(src), name, *job, dependencies, {});
}

struct WindowState
{
	HWND handle;
	S32 client_width;
	S32 client_height;
	S32 actual_width;
	S32 actual_height;
	S32 x, y;
	U32 dpi;
	F32 dpi_scale;
	DWORD style;
	S32 mouse_x, mouse_y;
	bool mouse_locked = false;
};

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	static WindowState* g_window_state = nullptr;
	switch (message)
	{
		case WM_CREATE:
		{
			CREATESTRUCT const* const create_info = reinterpret_cast<CREATESTRUCT const*>(lparam);
			PAW_ASSERT(g_window_state == nullptr, "window state is not null, why are you setting this twice?")
			g_window_state = reinterpret_cast<WindowState*>(create_info->lpCreateParams);
		}
		break;

		case WM_DESTROY:
		{
			PAW_ASSERT(g_window_state, "invalid window state ptr");
			PostQuitMessage(0);
			return false;
		}
		break;

		default:
		{
			return DefWindowProc(window, message, wparam, lparam);
		}
	}

	return false;
}

static void TestSubJob(JobGraph& /*graph*/, JobHandle /*job*/, StringView8 name)
{
	PAW_INFO("TestSubJob: parent: " PAW_STR_FMT "\n", PAW_FMT_STR(name));
}

static void TestJob(JobGraph& graph, JobHandle job, StringView8 name)
{
	PAW_INFO("TestJob: " PAW_STR_FMT "\n", PAW_FMT_STR(name));
	JobGraphAddJob(graph, SrcLoc(), PAW_STR("Test Sub Job"), {job}, TestSubJob, name);
}

#if 0



struct GraphicsPipelineState
{
	ID3D12PipelineState* pso;
	ID3D12RootSignature* root_signature;
};

static GraphicsPipelineState CreateGraphicsPipelineState(
	char const* shader_path, char const* /*name*/, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, IDxcIncludeHandler* include_handler, IDxcCompiler3* compiler, IDxcUtils* utils, ID3D12Device* device, IAllocator* allocator)
{
	CompiledShader const vertex_shader = CompileShader(shader_path, ShaderType_Vertex, include_handler, compiler, utils, allocator);
	CompiledShader const pixel_shader = CompileShader(shader_path, ShaderType_Pixel, include_handler, compiler, utils, allocator);

	U32 const num_constant_params = Max(vertex_shader.num_constant_params, pixel_shader.num_constant_params);

	D3D12_ROOT_PARAMETER root_param{
		.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
		.Constants =
			{
				.ShaderRegister = 0,
				.RegisterSpace = 0,
				.Num32BitValues = num_constant_params,
			},
		.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
	};

	const D3D12_ROOT_SIGNATURE_DESC root_signature_desc{
		.NumParameters = num_constant_params > 0 ? 1u : 0,
		.pParameters = &root_param,
		.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
			D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED,
	};

	ID3DBlob* signature_blob = nullptr;
	ID3DBlob* signature_error_blob = nullptr;
	DX_VERIFY(D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &signature_error_blob));

	Defer defer_signature_blob{[signature_blob]
							   { signature_blob->Release(); }};
	Defer defer_signature_error_blob{
		[signature_error_blob]
		{ signature_error_blob->Release(); },
		signature_error_blob != nullptr};

	ID3D12RootSignature* root_signature = nullptr;

	DX_VERIFY(device->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature)));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = desc;
	pso_desc.pRootSignature = root_signature;
	pso_desc.VS = {
		.pShaderBytecode = vertex_shader.blob->GetBufferPointer(),
		.BytecodeLength = vertex_shader.blob->GetBufferSize(),
	};
	pso_desc.PS = {
		.pShaderBytecode = pixel_shader.blob->GetBufferPointer(),
		.BytecodeLength = pixel_shader.blob->GetBufferSize(),
	};
	ID3D12PipelineState* pso = nullptr;
	DX_VERIFY(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso)));

	vertex_shader.blob->Release();
	pixel_shader.blob->Release();

	return {pso, root_signature};
}

#endif

// These should be ordered by stage order

static WindowState g_window_state{};

void Platform::Init(IAllocator* /*static_allocator*/, IAllocator* /*static_debug_allocator*/)
{
	const WNDCLASSEX window_class{
		.cbSize = sizeof(WNDCLASSEX),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = &WindowProc,
		.hInstance = GetModuleHandle(nullptr),
		.hCursor = LoadCursor(NULL, IDC_ARROW),
		.lpszClassName = "PawWindowClass",
	};

	RegisterClassEx(&window_class);

	g_window_state.x = 100;
	g_window_state.y = 100;
	g_window_state.client_width = 1600;
	g_window_state.client_height = 900;

	RECT window_rect{
		0,
		0,
		g_window_state.client_width,
		g_window_state.client_height,
	};

	g_window_state.style = WS_OVERLAPPEDWINDOW;
	// g_window_state.style = WS_POPUP;

	{
		RECT new_window_rect = window_rect;
		AdjustWindowRectExForDpi(&new_window_rect, g_window_state.style, FALSE, 0, g_window_state.dpi);
		window_rect = new_window_rect;
	}

	g_window_state.actual_width = window_rect.right - window_rect.left;
	g_window_state.actual_height = window_rect.bottom - window_rect.top;

	g_window_state.handle = CreateWindow(
		window_class.lpszClassName,
		"Pawprint Window",
		g_window_state.style,
		g_window_state.x,
		g_window_state.y,
		g_window_state.actual_width,
		g_window_state.actual_height,
		nullptr,
		nullptr,
		GetModuleHandle(nullptr),
		&g_window_state);

	ShowWindow(g_window_state.handle, SW_SHOW);
}

bool Platform::PumpEvents()
{
	bool should_quit = false;
	MSG msg{};
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		switch (msg.message)
		{
			case WM_QUIT:
			{
				should_quit = true;
			}
			break;

			default:
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			break;
		}
	}
	return should_quit;
}

Int2 Platform::GetViewportSize()
{
	return {g_window_state.client_width, g_window_state.client_height};
}

MemorySlice Platform::LoadFileBlocking(char const* path, IAllocator* allocator)
{
	HANDLE file = CreateFile(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	PAW_ASSERT(file != INVALID_HANDLE_VALUE, "Failed to CreateFile");

	LARGE_INTEGER file_size{};
	GetFileSizeEx(file, &file_size);
	PAW_ASSERT(file_size.QuadPart > 0, "File size is 0");

	MemorySlice file_mem = PAW_ALLOC_IN(allocator, file_size.QuadPart);
	DWORD bytes_read = 0;
	ReadFile(file, file_mem.ptr, static_cast<DWORD>(file_size.QuadPart), &bytes_read, nullptr);
	PAW_ASSERT(bytes_read == static_cast<DWORD>(file_size.QuadPart), "Bytes read does not match file size");
	CloseHandle(file);
	return file_mem;
}

int PlatformMain(int arg_count, char* args[])
{
	PAW_UNUSED_ARG(arg_count);
	PAW_UNUSED_ARG(args);

#if 0
	ArenaAllocator persistent_allocator{};
	ArenaAllocator debug_persistent_allocator{};

	JobQueue job_queue{&persistent_allocator};
	JobGraph job_graph{job_queue, &persistent_allocator};

	Job& parent_job = JobGraphAddJob(job_graph, SrcLoc(), PAW_STR("Parent"), {}, TestJob, PAW_STR("Parent"));
	Slice<JobHandle const> const deps{JobHandle{(PtrSize)&parent_job}};
	Job& child_job0 = JobGraphAddJob(job_graph, SrcLoc(), PAW_STR("Child 0"), deps, TestJob, PAW_STR("Child 0"));
	Job& child_job1 = JobGraphAddJob(job_graph, SrcLoc(), PAW_STR("Child 1"), deps, TestJob, PAW_STR("Child 1"));
	JobGraphAddJob(job_graph, SrcLoc(), PAW_STR("Child 1"), {JobHandle{(PtrSize)&child_job0}, JobHandle{(PtrSize)&child_job1}}, TestJob, PAW_STR("Grandchild"));
	job_graph.Schedule(parent_job);
#endif
	// IDXGIOutput* output = nullptr;
	// for (U32 i = 0; adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND; i++)
	//{
	//	DXGI_OUTPUT_DESC desc;
	//	output->GetDesc(&desc);
	//	U32 dpi_x, dpi_y;
	//	GetDpiForMonitor(desc.Monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);
	//	PAW_ASSERT(dpi_x == dpi_y, "DPI X does not match DPI Y");
	//	PAW_INFO("Monitor %u: %ls\n\tRect: %ld, %ld, %ld, %ld\n\tScale: %g\n", i, desc.DeviceName, desc.DesktopCoordinates.left, desc.DesktopCoordinates.top, desc.DesktopCoordinates.right - desc.DesktopCoordinates.left, desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top, static_cast<double>(dpi_x) / static_cast<double>(USER_DEFAULT_SCREEN_DPI));
	// }

	return 0;
}

extern "C"
{
	__declspec(dllexport) extern const UINT D3D12SDKVersion = 716;
}

extern "C"
{
	__declspec(dllexport) extern char const* D3D12SDKPath = ".\\";
}

Byte* PlatformReserveAddressSpace(PtrSize size_bytes)
{
	void* const address_space = VirtualAlloc2(nullptr, nullptr, size_bytes, MEM_RESERVE, PAGE_NOACCESS, nullptr, 0);
	Byte* const result = reinterpret_cast<Byte*>(address_space);
	return result;
}

void PlatformCommitAddressSpace(Byte* start_ptr, PtrSize size_bytes)
{
	VirtualAlloc(start_ptr, size_bytes, MEM_COMMIT, PAGE_READWRITE);
}

void PlatformDecommitAddressSpace(Byte* start_ptr, PtrSize size_bytes)
{
	VirtualFree(start_ptr, size_bytes, MEM_DECOMMIT);
}