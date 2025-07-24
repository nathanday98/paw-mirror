#include <core/gfx.h>

#include <core/memory.inl>
#include <core/logger.h>
#include <core/assert.h>
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
#include <cstring>
#pragma clang diagnostic pop

#include "gfx_dx12.h"
#include "render_graph.h"

static void __stdcall D3D12MessageCallback(
	D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID, LPCSTR description, void*)
{
	if (category == D3D12_MESSAGE_CATEGORY_STATE_CREATION)
	{
		return;
	}

	switch (severity)
	{
		case D3D12_MESSAGE_SEVERITY_CORRUPTION:
		case D3D12_MESSAGE_SEVERITY_ERROR:
		{
			PAW_ERROR("ERROR: D3D12: %s\n", description);
		}
		break;

		case D3D12_MESSAGE_SEVERITY_WARNING:
		{
			PAW_WARNING("WARN: D3D12: %s\n", description);
		}
		break;

		case D3D12_MESSAGE_SEVERITY_INFO:
		case D3D12_MESSAGE_SEVERITY_MESSAGE:
		{
			PAW_INFO("INFO: D3D12: %s\n", description);
		}
		break;

		default:
			PAW_UNREACHABLE;
	}
}

static constexpr D3D12_HEAP_PROPERTIES g_upload_heap_props{
	.Type = D3D12_HEAP_TYPE_UPLOAD,
	.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
	.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
	.CreationNodeMask = 1,
	.VisibleNodeMask = 1,
};

static void WaitForPresentQueue(Gfx::State& state)
{
	// schedule a signal in the command queue
	DX_VERIFY(state.present_command_queue->Signal(state.present_fence, state.fence_values[state.local_frame_index]));

	// wait until the fence has been processed
	DX_VERIFY(state.present_fence->SetEventOnCompletion(state.fence_values[state.local_frame_index], state.present_fence_event));
	WaitForSingleObjectEx(state.present_fence_event, INFINITE, FALSE);

	// increment the fence value for the current frame
	state.fence_values[state.local_frame_index]++;
}

Gfx::State& Gfx::Init(IAllocator* allocator, IAllocator* debug_allocator)
{
	State& state = *PAW_NEW_IN(allocator, State)();
	state.debug_allocator = debug_allocator;
	ID3D12Debug6* debug_controller = nullptr;
	{
		ID3D12Debug6* dc = nullptr;
		DX_VERIFY(D3D12GetDebugInterface(PAW_IID_PPV_ARGS(&dc)));
		DX_VERIFY(dc->QueryInterface(PAW_IID_PPV_ARGS(&debug_controller)));
	}

	if (debug_controller)
	{
		debug_controller->EnableDebugLayer();
		// #TODO: Enable this - I was having an internal crash in SetPipelineState, the discord said to disable this until it's fixed
		// debug_controller->SetEnableGPUBasedValidation(true);
	}

	IDXGIFactory7* factory = nullptr;
	const UINT dxgi_factory_flags = DXGI_CREATE_FACTORY_DEBUG;
	DX_VERIFY(CreateDXGIFactory2(dxgi_factory_flags, PAW_IID_PPV_ARGS(&factory)));

	bool found_adapter = false;

	IDXGIAdapter1* adapter;
	for (UINT adapter_index = 0;
		 factory->EnumAdapterByGpuPreference(
			 adapter_index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, PAW_IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
		 ++adapter_index)
	{
		CHAR vendor_name[128];
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (true)
		{
			size_t num_converted = 0;
			wcstombs_s(&num_converted, vendor_name, 128, desc.Description, 128);
			// PAW_LOG_INFO("%ls", vendor_name);
			PAW_INFO("GPU: %s\n", vendor_name);
			found_adapter = true;
			break;
		}

		adapter->Release();
	}

	PAW_ASSERT(found_adapter, "Failed to find appropriate GPU");

	DX_VERIFY(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, PAW_IID_PPV_ARGS(&state.device)));

	ID3D12DebugDevice2* debug_device = nullptr;
	DX_VERIFY(state.device->QueryInterface(&debug_device));

	// debug_state.device->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL);

	ID3D12InfoQueue1* info_queue = nullptr;
	DX_VERIFY(state.device->QueryInterface(&info_queue));

	if (info_queue)
	{
		DWORD cookie = 0;
		DX_VERIFY(info_queue->RegisterMessageCallback(&D3D12MessageCallback, D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS, nullptr, &cookie));
	}

	D3D12_FEATURE_DATA_D3D12_OPTIONS12 options_12{};
	DX_VERIFY(state.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options_12, sizeof(options_12)));

	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options_5{};
	DX_VERIFY(state.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options_5, sizeof(options_5)));

	D3D12_FEATURE_DATA_D3D12_OPTIONS7 options_7{};
	DX_VERIFY(state.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options_7, sizeof(options_7)));

	D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_6};
	DX_VERIFY(state.device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model)));

	PAW_ASSERT(options_12.EnhancedBarriersSupported, "Enhanced barriers not supported");

	state.command_list_allocator.Init(state.device, allocator);

	state.present_command_queue = state.command_list_allocator.GetPresentQueue();

	bool hdr_enabled = false;

	state.swapchain_format = hdr_enabled ? Gfx::Format::R10G10B10A2_Unorm : Gfx::Format::R8G8B8A8_Unorm;

	DXGI_FORMAT const swapchain_format_dx = g_texture_format_map[S32(state.swapchain_format)];
	// DXGI_FORMAT const swapchain_read_format = hdr_enabled ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// #TODO: Pass this stuff in properly
	HWND const window_handle = GetActiveWindow();
	RECT client_rect{};
	GetClientRect(window_handle, &client_rect);

	DXGI_SWAP_CHAIN_DESC1 swapchain_desc{
		.Width = static_cast<U32>(client_rect.right - client_rect.left),
		.Height = static_cast<U32>(client_rect.bottom - client_rect.top),
		.Format = swapchain_format_dx,
		.SampleDesc =
			{
				.Count = 1,
			},
		.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
		.BufferCount = g_frames_in_flight,
		.Scaling = DXGI_SCALING_STRETCH,
		.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
		.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING,
	};

	IDXGISwapChain1* initial_swapchain = nullptr;
	DX_VERIFY(factory->CreateSwapChainForHwnd(state.present_command_queue, window_handle, &swapchain_desc, nullptr, nullptr, &initial_swapchain));

	DX_VERIFY(initial_swapchain->QueryInterface(&state.swapchain));

	state.swapchain->SetMaximumFrameLatency(2);
	state.swapchain_event = state.swapchain->GetFrameLatencyWaitableObject();
	// DX_VERIFY(swapchain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020));

	state.device->SetStablePowerState(TRUE);

	factory->MakeWindowAssociation(window_handle, DXGI_MWA_NO_ALT_ENTER);

	state.descriptor_pool.Init(state.device, allocator);

	for (S32 i = 0; i < g_frames_in_flight; ++i)
	{
		DX_VERIFY(state.swapchain->GetBuffer(i, PAW_IID_PPV_ARGS(&state.backbuffer_resources[i])));

		SetDebugName(state.backbuffer_resources[i], PAW_STR("Backbuffer"));
	}

	state.local_frame_index = static_cast<S32>(state.swapchain->GetCurrentBackBufferIndex());
	DX_VERIFY(state.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, PAW_IID_PPV_ARGS(&state.present_fence)));
	state.present_fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	state.fence_values[state.local_frame_index]++;

	/*DescriptorIndex<DescriptorType::Sampler> sampler_index = g_null_sampler_descriptor_index;
	{
		const D3D12_SAMPLER_DESC desc{
			.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT,
			.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		};

		sampler_index = descriptor_pool.AllocSampler();
		device->CreateSampler(&desc, descriptor_pool.GetSamplerCPUHandle(sampler_index));
	}*/

	state.static_texture_pool.Init(allocator, state);

	for (S32 i = 0; i < g_frames_in_flight; i++)
	{
		state.frame_buffer_allocators[i].Init(state);
	}

	state.pipeline_pool.Init(allocator, 64);
	state.sampler_pool.Init(allocator, 64);

	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&state.utils));

	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&state.compiler));

	DX_VERIFY(state.utils->CreateDefaultIncludeHandler(&state.include_handler));

	WaitForPresentQueue(state);

	WaitForSingleObjectEx(state.swapchain_event, INFINITE, FALSE);

	return state;
}

void Gfx::Render(State& state)
{
	WaitForSingleObjectEx(state.swapchain_event, INFINITE, FALSE);

	if (state.present_fence->GetCompletedValue() < state.last_frames_fence_value)
	{
		DX_VERIFY(state.present_fence->SetEventOnCompletion(state.last_frames_fence_value, state.present_fence_event));
		WaitForSingleObjectEx(state.present_fence_event, INFINITE, FALSE);
	}

	state.fence_values[state.local_frame_index] = state.last_frames_fence_value + 1;

	state.frame_buffer_allocators[state.local_frame_index].Reset();

	CommandList command_list_handle = state.command_list_allocator.GrabAndResetGraphicsCommandList(state.local_frame_index);
	RenderGraph(state, state.current_graph, command_list_handle);

	state.command_list_allocator.CloseExecuteAndFreeCommandList(command_list_handle);

	DX_VERIFY(state.swapchain->Present(1, 0));

	state.last_frames_fence_value = state.fence_values[state.local_frame_index];

	DX_VERIFY(state.present_command_queue->Signal(state.present_fence, state.last_frames_fence_value));

	state.local_frame_index = static_cast<S32>(state.swapchain->GetCurrentBackBufferIndex());

	PAW_ASSERT(state.local_frame_index < g_frames_in_flight, "Unexpected frame index value");
}

void Gfx::Deinit(State& state)
{
	WaitForPresentQueue(state);
	PAW_DELETE(&state);
}

Gfx::Texture Gfx::CreateTexture(State& state, TextureDesc&& desc)
{
	return state.static_texture_pool.Alloc(PAW_MOVE(desc), state);
}

Gfx::TextureDescriptor Gfx::GetTextureDescriptor(State& state, Texture texture)
{
	return {state.static_texture_pool.GetSrvDescriptor(texture).value};
}

Gfx::Sampler Gfx::CreateSampler(State& state, SamplerDesc&& desc)
{
	Handle<SamplerDescriptor> const sampler = state.sampler_pool.Alloc();
	DescriptorIndex<DescriptorType::Sampler> const sampler_descriptor = state.descriptor_pool.AllocSampler();
	SamplerDescriptor& descriptor = state.sampler_pool.GetValue(sampler);
	descriptor = {sampler_descriptor.value};

	D3D12_SAMPLER_DESC const dx_desc{
		.Filter = g_filters[S32(desc.filter)],
		.AddressU = g_address_modes[S32(desc.address_u)],
		.AddressV = g_address_modes[S32(desc.address_v)],
		.AddressW = g_address_modes[S32(desc.address_w)],
	};
	state.device->CreateSampler(&dx_desc, state.descriptor_pool.GetSamplerCPUHandle(sampler_descriptor));
	return {sampler.index, sampler.generation};
}

Gfx::SamplerDescriptor Gfx::GetSamplerDescriptor(State& state, Sampler sampler)
{
	Handle<Gfx::SamplerDescriptor> const handle = {sampler.index, sampler.generation};
	return state.sampler_pool.GetValue(handle);
}

Gfx::Buffer Gfx::CreateBuffer(State&, BufferDesc&& /*desc*/)
{
	return {};
}

struct CompiledShader
{
	IDxcBlob* blob = nullptr;
	U32 num_constant_params;
	U32 num_threads_x;
	U32 num_threads_y;
	U32 num_threads_z;
};

enum ShaderType
{
	ShaderType_Vertex,
	ShaderType_Pixel,
	ShaderType_Amplification,
	ShaderType_Mesh,
	ShaderType_Compute,
	ShaderType_Count,
};

static constexpr char const* g_shader_type_names[ShaderType_Count]{
	"Vertex",
	"Pixel",
	"Amplification",
	"Mesh",
	"Compute",
};

static CompiledShader CompileShader(Gfx::State& state, MemorySlice shader_code, ShaderType type, StringView8 debug_name)
{

	HRESULT result = S_OK;
	DxcBuffer const source{
		.Ptr = shader_code.ptr,
		.Size = shader_code.size_bytes,
		.Encoding = DXC_CP_UTF8,
	};

	static constexpr LPCWSTR shader_entry_points[ShaderType_Count]{
		L"VSMain",
		L"PSMain",
		L"ASMain",
		L"MSMain",
		L"CSMain",
	};

	static constexpr LPCWSTR shader_versions[ShaderType_Count]{
		L"vs_6_6",
		L"ps_6_6",
		L"as_6_6",
		L"ms_6_6",
		L"cs_6_6",
	};

	IDxcBlob* shader_blob = nullptr;

	LPCWSTR compiler_args[]{
		L"-E",
		shader_entry_points[type],
		L"-Fd",
		L"temp\\shader_pdbs\\",
		L"-T",
		shader_versions[type],
		L"-HV",
		L"2021",
		L"-O1",
		L"-Zi",
	};

	IDxcResult* compile_result = nullptr;
	DX_VERIFY(state.compiler->Compile(&source, compiler_args, PAW_ARRAY_COUNT(compiler_args), state.include_handler, IID_PPV_ARGS(&compile_result)));

	/*Defer defer_compile_result{[compile_result]
							   { compile_result->Release(); }};*/

	IDxcBlobUtf8* errors = nullptr;
	DX_VERIFY(compile_result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr));

	Defer defer_errors{[errors]
					   { errors->Release(); }};

	if (errors->GetStringLength() != 0)
	{
		char const* error_str = errors->GetStringPointer();
		PtrSize const error_str_len = errors->GetStringLength();
		PAW_INFO("Shader (" PAW_STR_FMT ") (%s): %.*s", PAW_FMT_STR(debug_name), g_shader_type_names[type], static_cast<int>(error_str_len), error_str);
	}

	DX_VERIFY(compile_result->GetStatus(&result));
	/*if (FAILED(result))
	{
		PAW_LOG_ERROR("Failed to compile %s", shader_path.ptr);
	}*/

	DX_VERIFY(compile_result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader_blob), nullptr));

	//{
	//	FILE* file = nullptr;
	//	fopen_s(&file, "compiled_data/shaders/test.vertex.bin", "wb");
	//	fwrite(vertex_shader_blob->GetBufferPointer(), vertex_shader_blob->GetBufferSize(), 1, file);
	//	fclose(file);
	//}

	IDxcBlob* reflection_data_blob = nullptr;
	DX_VERIFY(compile_result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflection_data_blob), nullptr));
	Defer defer_reflection_data_blob{[reflection_data_blob]
									 { reflection_data_blob->Release(); }};
	DxcBuffer const reflection_data{
		.Ptr = reflection_data_blob->GetBufferPointer(),
		.Size = reflection_data_blob->GetBufferSize(),
		.Encoding = DXC_CP_ACP,
	};

	ID3D12ShaderReflection* reflection = nullptr;
	state.utils->CreateReflection(&reflection_data, IID_PPV_ARGS(&reflection));
	Defer defer_reflection{[reflection]
						   { reflection->Release(); }};

	D3D12_SHADER_DESC shader_desc{};
	reflection->GetDesc(&shader_desc);

	U32 num_constant_params = 0;
	if (shader_desc.ConstantBuffers > 0)
	{
		ID3D12ShaderReflectionConstantBuffer* constants = reflection->GetConstantBufferByIndex(0);
		if (constants)
		{
			D3D12_SHADER_BUFFER_DESC constants_desc{};
			DX_VERIFY(constants->GetDesc(&constants_desc));
			num_constant_params = constants_desc.Size / 4;
		}
	}

	UINT num_threads_x = 0;
	UINT num_threads_y = 0;
	UINT num_threads_z = 0;

	reflection->GetThreadGroupSize(&num_threads_x, &num_threads_y, &num_threads_z);

	// TODO: Use this once renderdoc supports d3d12 shader debugging
	IDxcBlob* pdb_blob = nullptr;
	IDxcBlobUtf16* pdb_name_blob = nullptr;
	DX_VERIFY(compile_result->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(&pdb_blob), &pdb_name_blob));
	Defer defer_pdb_blob{[&pdb_blob]
						 { pdb_blob->Release(); }};
	Defer defer_pdb_name_blob{[&pdb_name_blob]
							  { pdb_name_blob->Release(); }};
	{
		wchar_t path[MAX_PATH];
		LPCWSTR filename = pdb_name_blob->GetStringPointer();
		wsprintfW(path, L"temp\\shader_pdbs\\%s", filename);
		FILE* file = nullptr;
		_wfopen_s(&file, path, L"wb");
		fwrite(pdb_blob->GetBufferPointer(), pdb_blob->GetBufferSize(), 1, file);
		fclose(file);
	}

	return CompiledShader{
		.blob = shader_blob,
		.num_constant_params = num_constant_params,
		.num_threads_x = num_threads_x,
		.num_threads_y = num_threads_y,
		.num_threads_z = num_threads_z,
	};
}

static constexpr D3D12_BLEND g_blends[S32(Gfx::Blend::Count)]{
	/* Zero */ D3D12_BLEND_ZERO,
	/* One */ D3D12_BLEND_ONE,
	/* SrcColor */ D3D12_BLEND_SRC_COLOR,
	/* InvSrcColor */ D3D12_BLEND_INV_SRC_COLOR,
	/* SrcAlpha */ D3D12_BLEND_SRC_ALPHA,
	/* InvSrcAlpha */ D3D12_BLEND_INV_SRC_ALPHA,
};

static constexpr D3D12_BLEND_OP g_blend_ops[S32(Gfx::BlendOp::Count)]{
	/* Add */ D3D12_BLEND_OP_ADD,
	/* Subtract */ D3D12_BLEND_OP_SUBTRACT,
};

// #TODO: Make this take pre-compiled shaders
Gfx::Pipeline Gfx::CreateGraphicsPipeline(State& state, GraphicsPipelineDesc&& desc)
{
	Handle<PipelineStateObject> const handle = state.pipeline_pool.Alloc();
	PipelineStateObject& pso = state.pipeline_pool.GetValue(handle);
	D3D12_GRAPHICS_PIPELINE_STATE_DESC dx_desc{

		.SampleMask = desc.sampler_mask,
		.RasterizerState = {
			.FillMode = g_fill_modes[S32(desc.rasterizer_state.fill_mode)],
			.CullMode = g_cull_modes[S32(desc.rasterizer_state.cull_mode)],
		},
		.PrimitiveTopologyType = g_topology_types[S32(desc.topology)],
		.NumRenderTargets = static_cast<UINT>(desc.render_target_formats.count),
		.DSVFormat = g_texture_format_map[S32(desc.depth_stencil_format)],
		.SampleDesc = {
			.Count = desc.sample_count,
		},
	};

	for (S32 i = 0; i < desc.blend_states.count; i++)
	{
		BlendState const& blend_state = desc.blend_states[i];
		D3D12_RENDER_TARGET_BLEND_DESC& out_blend_desc = dx_desc.BlendState.RenderTarget[i];
		out_blend_desc.BlendEnable = blend_state.enabled;
		out_blend_desc.RenderTargetWriteMask = static_cast<UINT8>(blend_state.write_mask);
		out_blend_desc.SrcBlend = g_blends[S32(blend_state.color_src)];
		out_blend_desc.DestBlend = g_blends[S32(blend_state.color_dest)];
		out_blend_desc.BlendOp = g_blend_ops[S32(blend_state.color_op)];
		out_blend_desc.SrcBlendAlpha = g_blends[S32(blend_state.alpha_src)];
		out_blend_desc.DestBlendAlpha = g_blends[S32(blend_state.alpha_dest)];
		out_blend_desc.BlendOpAlpha = g_blend_ops[S32(blend_state.alpha_op)];
		out_blend_desc.LogicOp = D3D12_LOGIC_OP_NOOP;
	}

	for (S32 i = 0; i < desc.render_target_formats.count; i++)
	{
		dx_desc.RTVFormats[i] = g_texture_format_map[S32(desc.render_target_formats[i])];
	}

	MemorySlice shaders[ShaderType_Count]{};
	shaders[ShaderType_Vertex] = desc.vertex_mem;
	shaders[ShaderType_Pixel] = desc.pixel_mem;

	CompiledShader compiled_shaders[ShaderType_Count]{};

	U32 num_constant_params = 0;
	for (S32 i = 0; i < ShaderType_Count; i++)
	{
		if (shaders[i].ptr != nullptr)
		{
			PAW_ASSERT(shaders[i].size_bytes > 0, "Shader size is 0");
			compiled_shaders[i] = CompileShader(state, shaders[i], static_cast<ShaderType>(i), desc.debug_name);
			num_constant_params = Max(num_constant_params, compiled_shaders[i].num_constant_params);
		}
	}

	if (desc.vertex_mem.ptr)
	{
		dx_desc.VS = {
			.pShaderBytecode = compiled_shaders[ShaderType_Vertex].blob->GetBufferPointer(),
			.BytecodeLength = compiled_shaders[ShaderType_Vertex].blob->GetBufferSize(),
		};
	}

	if (desc.pixel_mem.ptr)
	{
		dx_desc.PS = {
			.pShaderBytecode = compiled_shaders[ShaderType_Pixel].blob->GetBufferPointer(),
			.BytecodeLength = compiled_shaders[ShaderType_Pixel].blob->GetBufferSize(),
		};
	}

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

	DX_VERIFY(state.device->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&pso.root_signature)));
	dx_desc.pRootSignature = pso.root_signature;
	DX_VERIFY(state.device->CreateGraphicsPipelineState(&dx_desc, PAW_IID_PPV_ARGS(&pso.pso)));

	signature_blob->Release();
	if (signature_error_blob)
	{
		signature_error_blob->Release();
	}

	return {handle.index, handle.generation};
}

void Gfx::BindPipeline(Gfx::State& state, CommandList command_list_handle, Pipeline pipeline)
{
	ID3D12GraphicsCommandList9* command_list = state.command_list_allocator.GetGraphicsCommandList(command_list_handle);
	PipelineStateObject& pso = state.pipeline_pool.GetValue({pipeline.index, pipeline.generation});
	command_list->SetGraphicsRootSignature(pso.root_signature);
	command_list->SetPipelineState(pso.pso);
}

void Gfx::SetConstants(Gfx::State& state, CommandList command_list_handle, void const* constants, PtrSize size_bytes)
{
	ID3D12GraphicsCommandList9* command_list = state.command_list_allocator.GetGraphicsCommandList(command_list_handle);
	PAW_ASSERT(size_bytes % 4 == 0, "Constants buffer size is not divisible by 4");
	command_list->SetGraphicsRoot32BitConstants(0, size_bytes / 4, constants, 0);
}

void Gfx::SetTopology(State& state, CommandList command_list_handle, Topology topology)
{
	ID3D12GraphicsCommandList9* command_list = state.command_list_allocator.GetGraphicsCommandList(command_list_handle);
	command_list->IASetPrimitiveTopology(g_topologies[S32(topology)]);
}

void Gfx::Draw(State& state, CommandList command_list_handle, U32 vert_count, U32 instance_count, U32 start_vertex, U32 start_instance)
{
	ID3D12GraphicsCommandList9* command_list = state.command_list_allocator.GetGraphicsCommandList(command_list_handle);
	command_list->DrawInstanced(vert_count, instance_count, start_vertex, start_instance);
}

void CommandListPool::init(ID3D12Device12* device, D3D12_COMMAND_LIST_TYPE type, IAllocator* allocator, PtrSize count)
{
	HRESULT result;
	slots = PAW_NEW_SLICE_IN(allocator, count, Slot);
	for (S32 i = 0; i < slots.count; ++i)
	{
		result = device->CreateCommandList1(0, type, D3D12_COMMAND_LIST_FLAG_NONE, PAW_IID_PPV_ARGS(&slots[i].command_list));
		PAW_ASSERT(SUCCEEDED(result), "CreateCommandList1 failed");
		slots[i].next_free_index = i + 1;
	}

	slots[slots.count - 1].next_free_index = null_slot;
	first_free_index = 0;
}

CommandListPool::~CommandListPool()
{
	PAW_DELETE_SLICE(slots);
}

Gfx::CommandList CommandListPool::alloc(ID3D12CommandList*& out_command_list)
{
	PAW_ASSERT(first_free_index != null_slot, "No free command lists");
	S32 const free_slot_index = first_free_index;
	Slot& free_slot = slots[free_slot_index];
	first_free_index = free_slot.next_free_index;
	out_command_list = free_slot.command_list;
	// #TODO: Handle generations
	return {free_slot_index};
}

Gfx::CommandList CommandListPool::allocGraphics(ID3D12GraphicsCommandList9*& out_command_list)
{
	ID3D12CommandList* command_list;
	Gfx::CommandList index = alloc(command_list);
	DX_VERIFY(command_list->QueryInterface(&out_command_list));
	return index;
}

void CommandListPool::free(Gfx::CommandList handle)
{
	S32 const index = handle.index;
	PAW_ASSERT(index != null_slot, "Invalid slot");
	PAW_ASSERT(index < slots.count, "Invalid slot");
	Slot& slot = slots[index];
	slot.next_free_index = first_free_index;
	first_free_index = index;
}

ID3D12GraphicsCommandList9* CommandListPool::GetGraphicsCommandList(Gfx::CommandList handle)
{
	S32 const index = handle.index;
	PAW_ASSERT(index != null_slot, "Invalid slot");
	PAW_ASSERT(index < slots.count, "Invalid slot");
	Slot& slot = slots[index];
	ID3D12GraphicsCommandList9* result = nullptr;
	DX_VERIFY(slot.command_list->QueryInterface(&result));
	return result;
}

ID3D12CommandList* CommandListPool::GetCommandList(Gfx::CommandList handle)
{
	S32 const index = handle.index;
	PAW_ASSERT(index != null_slot, "Invalid slot");
	PAW_ASSERT(index < slots.count, "Invalid slot");
	Slot& slot = slots[index];
	return slot.command_list;
}

static constexpr D3D12_COMMAND_LIST_TYPE queue_type_to_command_list_type[QueueType_Count]{
	/* QueueType_Graphics */ D3D12_COMMAND_LIST_TYPE_DIRECT,
	/* QueueType_Compute */ D3D12_COMMAND_LIST_TYPE_COMPUTE,
	/* QueueType_Copy */ D3D12_COMMAND_LIST_TYPE_COPY,
};

void CommandListAllocator::Init(ID3D12Device12* device, IAllocator* allocator)
{
	for (U32 type = 0; type < QueueType_Count; ++type)
	{
		const D3D12_COMMAND_QUEUE_DESC queue_desc{
			.Type = queue_type_to_command_list_type[type],
			.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
		};

		DX_VERIFY(device->CreateCommandQueue(&queue_desc, PAW_IID_PPV_ARGS(&command_queues[type])));

		DX_VERIFY(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, PAW_IID_PPV_ARGS(&queue_fences[type])));
	}

	for (S32 type = 0; type < QueueType_Count; ++type)
	{
		D3D12_COMMAND_LIST_TYPE const d3d12_type = queue_type_to_command_list_type[type];
		for (S32 frame_index = 0; frame_index < g_frames_in_flight; ++frame_index)
		{
			device->CreateCommandAllocator(d3d12_type, PAW_IID_PPV_ARGS(&command_allocators[type][frame_index]));
		}
		command_list_pools[type].init(device, d3d12_type, allocator, 4);
	}
}

CommandListAllocator::~CommandListAllocator()
{
}

ID3D12CommandQueue* CommandListAllocator::GetPresentQueue()
{
	return command_queues[QueueType_Graphics];
}

void CommandListAllocator::Reset(S32 local_frame_index)
{
	for (S32 i = 0; i < QueueType_Count; ++i)
	{
		command_allocators[i][local_frame_index]->Reset();
	}
}

Gfx::CommandList CommandListAllocator::GrabAndResetGraphicsCommandList(S32 local_frame_index)
{
	QueueType const queue_type = QueueType_Graphics;
	ID3D12GraphicsCommandList9* command_list = nullptr;
	Gfx::CommandList const command_list_slot = command_list_pools[queue_type].allocGraphics(command_list);
	ID3D12CommandAllocator* allocator = command_allocators[queue_type][local_frame_index];
	DX_VERIFY(command_list->Reset(allocator, nullptr));
	return command_list_slot;
}

void CommandListAllocator::CloseExecuteAndFreeCommandList(Gfx::CommandList handle)
{
	QueueType const queue_type = QueueType_Graphics;
	ID3D12CommandList* command_list = command_list_pools[queue_type].GetCommandList(handle);
	ID3D12GraphicsCommandList9* graphics_command_list = command_list_pools[queue_type].GetGraphicsCommandList(handle);
	ID3D12CommandQueue* command_queue = command_queues[queue_type];
	DX_VERIFY(graphics_command_list->Close());

	{
		// PAW_PROFILER_SCOPE("ExecuteCommandLists");
		command_queue->ExecuteCommandLists(1, &command_list);
	}

	command_list_pools[queue_type].free(handle);
}

ID3D12GraphicsCommandList9* CommandListAllocator::GetGraphicsCommandList(Gfx::CommandList handle)
{
	return command_list_pools[QueueType_Graphics].GetGraphicsCommandList(handle);
}

void StaticTexturePool::Init(IAllocator* in_allocator, Gfx::State& state)
{
	allocator.InitFromMemory(nullptr, heap_size_bytes);
	S32 const texture_count = 256;
	slots = PAW_NEW_SLICE_IN(in_allocator, texture_count, Slot);
	generations = PAW_NEW_SLICE_IN(in_allocator, texture_count, U32);

	for (S32 i = 0; i < slots.count - 1; ++i)
	{
		Slot& slot = slots[i];
		new (&slot.next_free_index) S32(i + 1);
	}
	new (&slots[slots.count - 1].next_free_index) S32(-1);

	D3D12_HEAP_DESC const heap_desc{
		.SizeInBytes = heap_size_bytes,
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

	DX_VERIFY(state.device->CreateHeap(&heap_desc, IID_PPV_ARGS(&heap)));
}

StaticTexturePool::~StaticTexturePool()
{
	PAW_DELETE_SLICE(slots);
	PAW_DELETE_SLICE(generations);
}

Gfx::Texture StaticTexturePool::Alloc(Gfx::TextureDesc&& desc, Gfx::State& state)
{
	PAW_ASSERT(first_free_index != null_slot, "No free slots");
	S32 const index = first_free_index;
	Slot& free_slot = slots[index];
	first_free_index = free_slot.next_free_index;

	SlotData* slot_data = new (&free_slot.data) SlotData();

	D3D12_RESOURCE_DESC1 dx_desc{
		.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
		.Alignment = 0,
		.Width = static_cast<UINT64>(desc.width),
		.Height = static_cast<UINT>(desc.height),
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.Format = g_texture_format_map[int(desc.format)],
		.SampleDesc = {
			.Count = 1,
			.Quality = 0,
		},
		.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
		.Flags = D3D12_RESOURCE_FLAG_NONE,
	};

	D3D12_RESOURCE_ALLOCATION_INFO const alloc_info = state.device->GetResourceAllocationInfo2(0, 1, &dx_desc, nullptr);
	MemorySlice const mem = allocator.Alloc(alloc_info.SizeInBytes, alloc_info.Alignment);
	UINT64 const offset = reinterpret_cast<UINT64>(mem.ptr);
	DX_VERIFY(state.device->CreatePlacedResource2(heap, offset, &dx_desc, D3D12_BARRIER_LAYOUT_COPY_DEST, nullptr, 0, nullptr, IID_PPV_ARGS(&slot_data->resource)));
	SetDebugName(slot_data->resource, desc.debug_name);

	if (desc.data.ptr)
	{

		U64 const upload_pitch = (U64(desc.width) * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
		U64 const upload_size = U64(desc.height) * upload_pitch;
		const D3D12_RESOURCE_DESC upload_buffer_desc{
			.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
			.Alignment = 0,
			.Width = upload_size,
			.Height = 1,
			.DepthOrArraySize = 1,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_UNKNOWN,
			.SampleDesc = {
				.Count = 1,
				.Quality = 0,
			},
			.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
			.Flags = D3D12_RESOURCE_FLAG_NONE,
		};

		ID3D12Resource* upload_buffer = nullptr;
		DX_VERIFY(state.device->CreateCommittedResource(&g_upload_heap_props, D3D12_HEAP_FLAG_NONE, &upload_buffer_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&upload_buffer)));
		SetDebugName(upload_buffer, PAW_STR("Texture upload buffer"));

		void* mapped = nullptr;
		const D3D12_RANGE range{0, upload_size};
		DX_VERIFY(upload_buffer->Map(0, &range, &mapped));

		for (S32 y = 0; y < desc.height; ++y)
		{
			std::memcpy((void*)((U64)mapped + y * upload_pitch), desc.data.ptr + y * desc.width * 4, desc.width * 4);
		}
		upload_buffer->Unmap(0, &range);

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
		state.device->GetCopyableFootprints((D3D12_RESOURCE_DESC*)&dx_desc, 0, 1, 0, &footprint, nullptr, nullptr, nullptr);

		const D3D12_TEXTURE_COPY_LOCATION source_location{
			.pResource = upload_buffer,
			.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
			.PlacedFootprint = footprint,
		};

		const D3D12_TEXTURE_COPY_LOCATION dest_location{
			.pResource = slot_data->resource,
			.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
			.SubresourceIndex = 0,
		};

		Gfx::CommandList const command_list_handle = state.command_list_allocator.GrabAndResetGraphicsCommandList(0);
		ID3D12GraphicsCommandList9* command_list = state.command_list_allocator.GetGraphicsCommandList(command_list_handle);

		command_list->CopyTextureRegion(&dest_location, 0, 0, 0, &source_location, nullptr);

		state.command_list_allocator.CloseExecuteAndFreeCommandList(command_list_handle);

		WaitForPresentQueue(state);

		slot_data->srv_index = state.descriptor_pool.AllocCbvSrvUav();

		D3D12_SHADER_RESOURCE_VIEW_DESC const srv_desc{
			.Format = g_srv_format_map[U32(desc.format)],
			.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
			.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = dx_desc.MipLevels,
			},
		};

		state.device->CreateShaderResourceView(slot_data->resource, &srv_desc, state.descriptor_pool.GetCbvSrvUavCPUHandle(slot_data->srv_index));
	}
	U32 const generation = generations[index];
	return {index, generation};
}

void StaticTexturePool::Free(Gfx::Texture handle)
{
	U32& generation = generations[handle.index];
	PAW_ASSERT(generation == handle.generation, "Generations do not match");
	PAW_ASSERT(handle.index != null_slot, "Index is null");
	PAW_ASSERT(handle.index < slots.count, "Invalid slot");

	Slot& slot = slots[handle.index];
	new (&slot.next_free_index) S32(first_free_index);
	first_free_index = handle.index;
	generations[handle.index]++;
}

DescriptorIndex<DescriptorType::CbvSrvUav> StaticTexturePool::GetSrvDescriptor(Gfx::Texture handle)
{
	U32& generation = generations[handle.index];
	PAW_ASSERT(generation == handle.generation, "Generations do not match");
	PAW_ASSERT(handle.index != null_slot, "Index is null");
	PAW_ASSERT(handle.index < slots.count, "Invalid slot");
	Slot& slot = slots[handle.index];
	return slot.data.srv_index;
}

void DescriptorPool::Init(ID3D12Device12* device, IAllocator* allocator)
{
	cbv_srv_uav_descriptor_pool.Init(device, allocator, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 32);
	sampler_descriptor_pool.Init(device, allocator, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 8);
	rtv_descriptor_pool.Init(device, allocator, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 32);
	dsv_descriptor_pool.Init(device, allocator, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 32);
}

DescriptorIndex<DescriptorType::CbvSrvUav> DescriptorPool::AllocCbvSrvUav()
{
	return cbv_srv_uav_descriptor_pool.Alloc();
}

void DescriptorPool::FreeCbvSrvUav(DescriptorIndex<DescriptorType::CbvSrvUav> index)
{
	cbv_srv_uav_descriptor_pool.Free(index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorPool::GetCbvSrvUavCPUHandle(DescriptorIndex<DescriptorType::CbvSrvUav> index)
{
	return cbv_srv_uav_descriptor_pool.GetCPU(index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorPool::GetCbvSrvUavGPUHandle(DescriptorIndex<DescriptorType::CbvSrvUav> index)
{
	return cbv_srv_uav_descriptor_pool.GetGPU(index);
}

ID3D12DescriptorHeap* DescriptorPool::GetCbvSrvUavHeap()
{
	return cbv_srv_uav_descriptor_pool.GetHeap();
}

DescriptorIndex<DescriptorType::Rtv> DescriptorPool::AllocRtv()
{
	return rtv_descriptor_pool.Alloc();
}

void DescriptorPool::FreeRtv(DescriptorIndex<DescriptorType::Rtv> index)
{
	rtv_descriptor_pool.Free(index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorPool::GetRtvCPUHandle(DescriptorIndex<DescriptorType::Rtv> index)
{
	return rtv_descriptor_pool.GetCPU(index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorPool::GetRtvGPUHandle(DescriptorIndex<DescriptorType::Rtv> index)
{
	return rtv_descriptor_pool.GetGPU(index);
}

ID3D12DescriptorHeap* DescriptorPool::GetRtvHeap()
{
	return rtv_descriptor_pool.GetHeap();
}

DescriptorIndex<DescriptorType::Dsv> DescriptorPool::AllocDsv()
{
	return dsv_descriptor_pool.Alloc();
}

void DescriptorPool::FreeDsv(DescriptorIndex<DescriptorType::Dsv> index)
{
	dsv_descriptor_pool.Free(index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorPool::GetDsvCPUHandle(DescriptorIndex<DescriptorType::Dsv> index)
{
	return dsv_descriptor_pool.GetCPU(index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorPool::GetDsvGPUHandle(DescriptorIndex<DescriptorType::Dsv> index)
{
	return dsv_descriptor_pool.GetGPU(index);
}

ID3D12DescriptorHeap* DescriptorPool::GetDsvHeap()
{
	return dsv_descriptor_pool.GetHeap();
}

DescriptorIndex<DescriptorType::Sampler> DescriptorPool::AllocSampler()
{
	return sampler_descriptor_pool.Alloc();
}

void DescriptorPool::FreeSampler(DescriptorIndex<DescriptorType::Sampler> index)
{
	sampler_descriptor_pool.Free(index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorPool::GetSamplerCPUHandle(DescriptorIndex<DescriptorType::Sampler> index)
{
	return sampler_descriptor_pool.GetCPU(index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorPool::GetSamplerGPUHandle(DescriptorIndex<DescriptorType::Sampler> index)
{
	return sampler_descriptor_pool.GetGPU(index);
}

ID3D12DescriptorHeap* DescriptorPool::GetSamplerHeap()
{
	return sampler_descriptor_pool.GetHeap();
}

void TransientAllocator::Init(Gfx::State& state)
{
	D3D12_RESOURCE_DESC const desc{
		.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Width = size_bytes,
		.Height = 1,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.Format = DXGI_FORMAT_UNKNOWN,
		.SampleDesc = {.Count = 1},
		.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		.Flags = D3D12_RESOURCE_FLAG_NONE,
	};
	DX_VERIFY(state.device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource)));
	descriptor_index = state.descriptor_pool.AllocCbvSrvUav();
	const D3D12_SHADER_RESOURCE_VIEW_DESC buffer_view_desc{
		.Format = DXGI_FORMAT_R32_TYPELESS,
		.ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
		.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
		.Buffer = {
			.FirstElement = 0,
			.NumElements = size_bytes / 4,
			.Flags = D3D12_BUFFER_SRV_FLAG_RAW,
		},
	};

	SetDebugName(resource, PAW_STR("Transient Buffer"));

	state.device->CreateShaderResourceView(resource, &buffer_view_desc, state.descriptor_pool.GetCbvSrvUavCPUHandle(descriptor_index));
	void* memory = nullptr;
	DX_VERIFY(resource->Map(0, nullptr, &memory));
	allocator.InitFromMemory(reinterpret_cast<Byte*>(memory), size_bytes);
}

Gfx::RawBufferAlloc TransientAllocator::Alloc(PtrSize size_bytes, PtrSize align_bytes)
{
	MemorySlice const mem = allocator.Alloc(size_bytes, align_bytes);
	PtrDiff const offset = mem.ptr - allocator.GetBasePtr();
	PAW_ASSERT(offset < UINT32_MAX, "Allocation is too big for 32 bit offset, consider switching to gpu virtual address");
	return {mem.ptr, {descriptor_index.value}, static_cast<U32>(offset)};
}

Gfx::GraphBuilder& Gfx::CreateGraphBuilder(State& state, IAllocator* allocator)
{
	// #TODO: Actually pass this in properly
	RECT client_rect{};
	GetClientRect(GetActiveWindow(), &client_rect);
	state.graph_allocator.FreeAll();
	GraphBuilder& builder = *PAW_NEW_IN(allocator, GraphBuilder)({
																	 .width = client_rect.right - client_rect.left,
																	 .height = client_rect.bottom - client_rect.top,
																	 .format = state.swapchain_format,
																	 .name = PAW_STR("Backbuffer"),
																	 .initial_state = InitialState::Clear,
																	 .clear_value = {.color = {1.0f, 1.0f, 0.53f, 1.0f}},
																	 .sample_count = 1,
																 },
																 allocator,
																 &state.graph_allocator);
	return builder;
}

void Gfx::DestroyGraphBuilder(GraphBuilder& builder)
{
	PAW_DELETE(&builder);
}

void Gfx::BuildGraph(State& state, GraphBuilder& builder)
{
	state.current_graph = builder.Build(state);
}

Gfx::GraphPass& Gfx::GraphCreatePass(GraphBuilder& builder, StringView8 name)
{
	return builder.AddPass(name);
}

Gfx::GraphTexture Gfx::GraphCreateTexture(GraphBuilder& builder, GraphPass& pass, GraphTextureDesc&& desc)
{
	return builder.CreateTexture(pass, PAW_MOVE(desc));
}

Gfx::GraphTexture Gfx::GraphReadTexture(GraphBuilder& builder, GraphPass& pass, GraphTexture texture, Gfx::Access access)
{
	return builder.ReadTexture(pass, texture, access);
}

Gfx::GraphTexture Gfx::GraphWriteTexture(GraphBuilder& builder, GraphPass& pass, GraphTexture texture, Gfx::Access access)
{
	return builder.WriteTexture(pass, texture, access);
}

Gfx::GraphTexture Gfx::GraphGetBackBuffer(GraphBuilder& builder)
{
	return builder.GetBackBuffer();
}

IAllocator* Gfx::GetGraphAllocator(GraphBuilder& builder)
{
	return builder.GetGraphAllocator();
}

Gfx::TextureDescriptor Gfx::GetGraphTextureDescriptor(Gfx::State& state, Gfx::GraphTexture texture)
{
	return RenderGraphGetTextureDescriptor(state.current_graph, texture);
}

void Gfx::GraphSetExecutorPtr(GraphBuilder& builder, GraphPass& pass, GraphExecutor* executor)
{
	builder.SetExecutor(pass, executor);
}

Gfx::RawBufferAlloc Gfx::AllocTempBuffer(State& state, PtrSize size_bytes, PtrSize align_bytes)
{
	return state.frame_buffer_allocators[state.local_frame_index].Alloc(size_bytes, align_bytes);
}
