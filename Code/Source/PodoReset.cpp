#define NOMINMAX
#include "Podo.h"
#include "root.h"
#include "Asset.h"
#include "Option.h"
#include "Object.h"
#include "Alloc.h"
#include "Debug.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <d3dx12_root_signature.h>
#include <d3dx12_default.h>
#include <d3dx12_core.h>
#include <d3d12.h>
#include <d3dcommon.h>
#include <ResourceUploadBatch.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <dxgi1_5.h>
#include <dxgi1_4.h>
#include <dxgi1_3.h>
#include <dxgi1_2.h>
#include <dxgi.h>
#include <dxgicommon.h>
#include <dxgiformat.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <windows.h>
#include <algorithm>
#include <string>
#include <utility>
#include <cstdlib>
#include <climits>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using std::wstring;

void Podo::Reset()
{
	FlushCommandQueue();

	if (NeedResetScreenMode() == true)
	{
		ResetScreenMode();

		m_needResetFactory		= true;
		m_needResetSwapChain	= true;
	}

	if (NeedResetFactory() == true)
	{
		ResetFactory();
		ResetAdapterAndOutput();

		m_needResetDevice		= true;
		m_needResetSwapChain	= true;
	}

	if (NeedResetDevice() == true)
	{
		ResetDevice();
		ResetFence();
		ResetFenceEvent();
		ResetCommandQueue();
		ResetCommandAllocator();
		ResetCommandList();

		ResetDescriptorHeapRTV();
		ResetDescriptorHeapDSV();
		ResetDescriptorHeapCBVSRVUAV();

		m_needResetSwapChain	= true;
		m_needResetAsset		= true;
		m_needResetPSO			= true;
	}
	
	if (NeedResetSwapChain() == true)
	{
		ResetHDRSwapChainSupport();
		ResetSwapChain();
		ResetBackBufferInfo();
		ResetViewPort();
		ResetScissorRectangle();
		ResetDepthStencilBuffer();

		ResetRTV();
		ResetDSV();

		m_needResetPSO = true;
	}

	if (NeedResetAsset() == true)
	{
		ResetAssets();
		ResetObjects();

		ResetCBVSRVUAV();
	}

	if (NeedResetPSO() == true)
	{
		ResetRootSignature();
		ResetPipelineStateObject();
		ResetImGui();
	}

	m_needResetScreenMode	= false;
	m_needResetFactory		= false;
	m_needResetDevice		= false;
	m_needResetSwapChain	= false;
	m_needResetAsset		= false;
	m_needResetPSO			= false;

	m_optionFullScreen.DebugPrint();
	m_optionWindowSave.DebugPrint();
	m_optionVSync.DebugPrint();
	m_optionTearing.DebugPrint();
	m_optionHDR.DebugPrint();
	m_optionRayTracing.DebugPrint();
	m_optionMeshShader.DebugPrint();
	m_optionGUI.DebugPrint();
}

void Podo::ResetScreenMode()
{
	if (m_optionFullScreen.IsActive() == true)
	{
		ResetFullScreenMode();
	}
	else
	{
		ResetWindowMode();
	}
}

void Podo::ResetFullScreenMode()
{
	SetWindowLongPtr(m_hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);

	HMONITOR monitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
	MONITORINFO monitorInfo = {};
	monitorInfo.cbSize = sizeof(MONITORINFO);
	GetMonitorInfo(monitor, &monitorInfo);

	LONG monitorBaseX = monitorInfo.rcMonitor.left;
	LONG monitorBaseY = monitorInfo.rcMonitor.top;
	LONG monitorWidth = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
	LONG monitorHeight = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;

	SetWindowPos
	(
		m_hWnd,
		HWND_TOP,
		monitorBaseX,
		monitorBaseY,
		monitorWidth,
		monitorHeight,
		SWP_NOOWNERZORDER | SWP_FRAMECHANGED
	);
}

void Podo::ResetWindowMode()
{
	SetWindowLongPtr(m_hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);

	SetWindowPos
	(
		m_hWnd,
		HWND_TOP,
		m_optionWindowSave.GetWindowPosX(),
		m_optionWindowSave.GetWindowPosY(),
		m_optionWindowSave.GetWindowWidth(),
		m_optionWindowSave.GetWindowHeight(),
		SWP_NOOWNERZORDER | SWP_FRAMECHANGED
	);
}

void Podo::ResetFactory()
{
	UINT factoryFlags = 0;

#ifdef _DEBUG
	factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

	ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(m_dxgiFactory.ReleaseAndGetAddressOf())));

	//NOTE: 쿼리 출력 매개변수는 BOOL 타입이므로, bool 타입인 m_optionTearing.featureSupported를 인자로 사용하면 안 됨
	BOOL tearingQuery = FALSE;
	m_dxgiFactory->CheckFeatureSupport(
		DXGI_FEATURE_PRESENT_ALLOW_TEARING,
		&tearingQuery,
		sizeof(tearingQuery)
	);
	m_optionTearing.SetFeatureSupported(tearingQuery);
}

void Podo::ResetAdapterAndOutput()
{
	m_dxgiAdapter.Reset();

	ComPtr<IDXGIAdapter3> tempAdapter = nullptr;

	HRESULT result = S_OK;
	for (int i = 0; result != DXGI_ERROR_NOT_FOUND; i++)
	{
		result = m_dxgiFactory->EnumAdapterByGpuPreference
		(
			i,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(tempAdapter.ReleaseAndGetAddressOf())
		);

		if (SUCCEEDED(result) == true)
		{
			if (ResetOutput(tempAdapter.Get()) == true)
			{
				m_dxgiAdapter = tempAdapter;

				return;
			}
		}
	}

	throw std::runtime_error("can't find pAdapter that connected with most intersecting output");
}

bool Podo::ResetOutput(IDXGIAdapter3* pAdapter)
{
	m_dxgiOutput.Reset();
	m_dxgiOutput6.Reset();

	ComPtr<IDXGIOutput> tempOutput = nullptr;

	HMONITOR targetMonitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);

	HRESULT enumResult = S_OK;
	for (int i = 0; enumResult != DXGI_ERROR_NOT_FOUND; i++)
	{
		enumResult = pAdapter->EnumOutputs(i, tempOutput.ReleaseAndGetAddressOf());

		if (SUCCEEDED(enumResult) == true)
		{
			DXGI_OUTPUT_DESC tempOutputDesc;
			tempOutput->GetDesc(&tempOutputDesc);

			if (tempOutputDesc.Monitor == targetMonitor)
			{
				m_dxgiOutput = tempOutput;

				HRESULT asResult = m_dxgiOutput.As(&m_dxgiOutput6);
				if (SUCCEEDED(asResult) == true)
				{
					m_dxgiOutput6->GetDesc1(&m_dxgiOutputDesc);
					if (m_dxgiOutputDesc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
					{
						m_optionHDR.SetOutputSupported(false);
					}
					else
					{
						m_optionHDR.SetOutputSupported(true);
					}
				}
				else
				{
					m_optionHDR.SetOutputSupported(false);
				}

				return true;
			}
		}
	}

	return false;
}

void Podo::ResetDevice()
{
	m_device5.Reset();
	m_device2.Reset();
	m_device.Reset();

#ifdef _DEBUG
	ComPtr<ID3D12Debug1> debug;
	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf())));
	debug->EnableDebugLayer();
	debug->SetEnableGPUBasedValidation(true);
#endif

	ThrowIfFailed
	(
		D3D12CreateDevice(m_dxgiAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(m_device.ReleaseAndGetAddressOf()))
	);

	{
		D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_6 };
		ThrowIfFailed(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)));
	}

	{
		D3D12_FEATURE_DATA_FORMAT_SUPPORT backBufferFormatSDRQuery =
		{
			m_screenBackBufferFormatSDR,
			D3D12_FORMAT_SUPPORT1_NONE,
			D3D12_FORMAT_SUPPORT2_NONE
		};
		D3D12_FEATURE_DATA_FORMAT_SUPPORT backBufferFormatHDRQuery =
		{
			m_screenBackBufferFormatHDR,
			D3D12_FORMAT_SUPPORT1_NONE,
			D3D12_FORMAT_SUPPORT2_NONE
		};
		D3D12_FEATURE_DATA_FORMAT_SUPPORT depthStencilFormatQuery =
		{
			m_screenDepthStencilBufferFormat,
			D3D12_FORMAT_SUPPORT1_NONE,
			D3D12_FORMAT_SUPPORT2_NONE
		};

		ThrowIfFailed
		(
			m_device->CheckFeatureSupport
			(
				D3D12_FEATURE_FORMAT_SUPPORT, &depthStencilFormatQuery, sizeof(depthStencilFormatQuery)
			)
		);
		ThrowIfFailed
		(
			m_device->CheckFeatureSupport
			(
				D3D12_FEATURE_FORMAT_SUPPORT, &backBufferFormatSDRQuery, sizeof(backBufferFormatSDRQuery)
			)
		);
		HRESULT hdrQueryResult = m_device->CheckFeatureSupport
		(
			D3D12_FEATURE_FORMAT_SUPPORT, &backBufferFormatHDRQuery, sizeof(backBufferFormatHDRQuery)
		);

		ThrowIfFalse(depthStencilFormatQuery.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL);
		ThrowIfFalse(backBufferFormatSDRQuery.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET);
		if (SUCCEEDED(hdrQueryResult) == true)
		{
			m_optionHDR.SetFormatSupported((backBufferFormatHDRQuery.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) != 0);
		}
		else
		{
			m_optionHDR.SetFormatSupported(false);
		}
	}

	{
		m_optionMeshShader.SetDeviceSupported(SUCCEEDED(m_device.As(&m_device2)));
		m_optionRayTracing.SetDeviceSupported(SUCCEEDED(m_device.As(&m_device5)));

		if (m_optionMeshShader.deviceSupported == true)
		{
			D3D12_FEATURE_DATA_D3D12_OPTIONS7 meshShaderFeatureQuery = {};
			m_device2->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &meshShaderFeatureQuery, sizeof(meshShaderFeatureQuery));
			m_optionMeshShader.SetFeatureSupported(meshShaderFeatureQuery.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED);
		}
		else
		{
			m_optionMeshShader.SetFeatureSupported(false);
		}

		if (m_optionRayTracing.deviceSupported == true)
		{
			D3D12_FEATURE_DATA_D3D12_OPTIONS5 rayTracingFeatureQuery = {};
			m_device5->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &rayTracingFeatureQuery, sizeof(rayTracingFeatureQuery));
			m_optionRayTracing.SetFeatureSupported(rayTracingFeatureQuery.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED);
		}
		else
		{
			m_optionRayTracing.SetFeatureSupported(false);
		}
	}
}

void Podo::ResetFence()
{
	ThrowIfFailed(m_device->CreateFence(m_fenceCurrent, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.ReleaseAndGetAddressOf())));
}

void Podo::ResetFenceEvent()
{
	CloseFenceEvent();

	m_fenceEvent = CreateEventExW
	(
		nullptr,
		nullptr,
		0,
		EVENT_MODIFY_STATE | SYNCHRONIZE
	);

	ThrowIfNull(m_fenceEvent);
}

void Podo::ResetCommandQueue()
{
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
	commandQueueDesc.Type		= D3D12_COMMAND_LIST_TYPE_DIRECT;
	commandQueueDesc.Priority	= D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	commandQueueDesc.Flags		= D3D12_COMMAND_QUEUE_FLAG_NONE;
	commandQueueDesc.NodeMask	= 0;
	ThrowIfFailed
	(
		m_device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(m_commandQueue.ReleaseAndGetAddressOf()))
	);
}

void Podo::ResetCommandAllocator()
{
	ThrowIfFailed
	(
		m_device->CreateCommandAllocator
		(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(m_commandAllocator.ReleaseAndGetAddressOf())
		)
	);
}

void Podo::ResetCommandList()
{
	m_commandList6.Reset();
	m_commandList4.Reset();
	m_commandList.Reset();

	ThrowIfFailed
	(
		m_device->CreateCommandList
		(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			m_commandAllocator.Get(),
			nullptr,
			IID_PPV_ARGS(m_commandList.ReleaseAndGetAddressOf())
		)
	);

	m_optionRayTracing.SetCommandListSupported(SUCCEEDED(m_commandList.As(&m_commandList4)));
	m_optionMeshShader.SetCommandListSupported(SUCCEEDED(m_commandList.As(&m_commandList6)));

	m_commandList->Close();
}

void Podo::ResetDescriptorHeapRTV()
{
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
	descriptorHeapDesc.NumDescriptors = m_screenBackBufferCount;
	descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	descriptorHeapDesc.NodeMask = 0;

	ThrowIfFailed
	(
		m_device->CreateDescriptorHeap
		(
			&descriptorHeapDesc,
			IID_PPV_ARGS(m_descriptorHeapRTV.ReleaseAndGetAddressOf())
		)
	);

	m_descriptorHeapRTVIncrementSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_descriptorHeapRTVStartHandleCPU = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_descriptorHeapRTV->GetCPUDescriptorHandleForHeapStart());
}

void Podo::ResetDescriptorHeapDSV()
{
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
	descriptorHeapDesc.NumDescriptors = 1;
	descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	descriptorHeapDesc.NodeMask = 0;

	ThrowIfFailed
	(
		m_device->CreateDescriptorHeap
		(
			&descriptorHeapDesc,
			IID_PPV_ARGS(m_descriptorHeapDSV.ReleaseAndGetAddressOf())
		)
	);

	m_descriptorHeapDSVIncrementSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_descriptorHeapDSVStartHandleCPU = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_descriptorHeapDSV->GetCPUDescriptorHandleForHeapStart());
}

void Podo::ResetDescriptorHeapCBVSRVUAV()
{
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
	UINT totalCount = m_descriptorHeapCBVSRVUAVCapacityForGUI + m_descriptorHeapCBVSRVUAVCapacityForRender;
	descriptorHeapDesc.NumDescriptors = totalCount;
	descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descriptorHeapDesc.NodeMask = 0;

	ThrowIfFailed
	(
		m_device->CreateDescriptorHeap
		(
			&descriptorHeapDesc,
			IID_PPV_ARGS(m_descriptorHeapCBVSRVUAV.ReleaseAndGetAddressOf())
		)
	);

	//NOTE: ImGui가 SRV를 둘 곳을 고정적으로 남겨두고, 그 뒷부분부터 사용하기로 함
	m_descriptorHeapCBVSRVUAVIncrementSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_descriptorHeapCBVSRVUAVStartHandleCPUForGUI = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_descriptorHeapCBVSRVUAV->GetCPUDescriptorHandleForHeapStart());
	m_descriptorHeapCBVSRVUAVStartHandleGPUForGUI = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptorHeapCBVSRVUAV->GetGPUDescriptorHandleForHeapStart());

	m_descriptorHeapCBVSRVUAVStartHandleCPUForRender = m_descriptorHeapCBVSRVUAVStartHandleCPUForGUI;
	m_descriptorHeapCBVSRVUAVStartHandleCPUForRender.Offset(m_descriptorHeapCBVSRVUAVCapacityForGUI, m_descriptorHeapCBVSRVUAVIncrementSize);
	m_descriptorHeapCBVSRVUAVStartHandleGPUForRender = m_descriptorHeapCBVSRVUAVStartHandleGPUForGUI;
	m_descriptorHeapCBVSRVUAVStartHandleGPUForRender.Offset(m_descriptorHeapCBVSRVUAVCapacityForGUI, m_descriptorHeapCBVSRVUAVIncrementSize);
}

void Podo::ResetHDRSwapChainSupport()
{
	for (UINT i = 0; i < m_screenBackBufferCount; i++)
	{
		m_screenBackBuffers[i].Reset();
	}

	m_screenSwapChain.Reset();

	RECT rectClient = {};
	ThrowIfFalse(GetClientRect(m_hWnd, &rectClient));
	LONG widthClient = rectClient.right - rectClient.left;
	LONG heightClient = rectClient.bottom - rectClient.top;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width					= std::max((int)widthClient, 10);
	swapChainDesc.Height				= std::max((int)heightClient, 10);
	swapChainDesc.Format				= m_screenBackBufferFormatHDR;
	swapChainDesc.Stereo				= false;
	swapChainDesc.SampleDesc.Count		= 1;
	swapChainDesc.SampleDesc.Quality	= 0;
	swapChainDesc.BufferUsage			= DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount			= m_screenBackBufferCount;
	swapChainDesc.Scaling				= DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect			= DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode				= DXGI_ALPHA_MODE_UNSPECIFIED;
	swapChainDesc.Flags					= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
										| (m_optionTearing.IsActive() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);

	ComPtr<IDXGISwapChain1> tempSwapChain = nullptr;

	ThrowIfFailed
	(
		m_dxgiFactory->CreateSwapChainForHwnd
		(
			m_commandQueue.Get(),
			m_hWnd,
			&swapChainDesc,
			nullptr,
			nullptr,
			tempSwapChain.ReleaseAndGetAddressOf()
		)
	);

	ComPtr<IDXGISwapChain3> tempSwapChain3 = nullptr;

	ThrowIfFailed((tempSwapChain.As(&tempSwapChain3)));

	UINT colorSpaceHDRQuery = 0;
	HRESULT queryResult = tempSwapChain3->CheckColorSpaceSupport(m_screenBackBufferColorSpaceHDR, &colorSpaceHDRQuery);
	if (SUCCEEDED(queryResult) == true)
	{
		m_optionHDR.SetColorSpaceSupported((colorSpaceHDRQuery & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0);
	}
	else
	{
		m_optionHDR.SetColorSpaceSupported(false);
	}
}

void Podo::ResetSwapChain()
{
	for (UINT i = 0; i < m_screenBackBufferCount; i++)
	{
		m_screenBackBuffers[i].Reset();
	}

	m_screenSwapChain.Reset();

	RECT rectClient = {};
	ThrowIfFalse(GetClientRect(m_hWnd, &rectClient));
	LONG widthClient = rectClient.right - rectClient.left;
	LONG heightClient = rectClient.bottom - rectClient.top;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width					= std::max((int)widthClient, 10);
	swapChainDesc.Height				= std::max((int)heightClient, 10);;
	swapChainDesc.Format				= m_optionHDR.IsActive() ? m_screenBackBufferFormatHDR : m_screenBackBufferFormatSDR;
	swapChainDesc.Stereo				= false;
	swapChainDesc.SampleDesc.Count		= 1;
	swapChainDesc.SampleDesc.Quality	= 0;
	swapChainDesc.BufferUsage			= DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount			= m_screenBackBufferCount;
	swapChainDesc.Scaling				= DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect			= DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode				= DXGI_ALPHA_MODE_UNSPECIFIED;
	swapChainDesc.Flags					= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
										| (m_optionTearing.IsActive() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);

	ComPtr<IDXGISwapChain1> tempSwapChain = nullptr;

	ThrowIfFailed
	(
		m_dxgiFactory->CreateSwapChainForHwnd
		(
			m_commandQueue.Get(),
			m_hWnd,
			&swapChainDesc,
			nullptr,
			nullptr,
			tempSwapChain.ReleaseAndGetAddressOf()
		)
	);

	ThrowIfFailed
	(
		m_dxgiFactory->MakeWindowAssociation
		(
			m_hWnd,
			DXGI_MWA_NO_ALT_ENTER
		)
	);

	ThrowIfFailed(tempSwapChain.As(&m_screenSwapChain));

	if (m_optionHDR.IsActive() == true)
	{
		m_screenSwapChain->SetColorSpace1(m_screenBackBufferColorSpaceHDR);
	}
	else
	{
		m_screenSwapChain->SetColorSpace1(m_screenBackBufferColorSpaceSDR);
	}
}

void Podo::ResetBackBufferInfo()
{
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	m_screenSwapChain->GetDesc1(&swapChainDesc);
	m_screenBackBufferWidth = swapChainDesc.Width;
	m_screenBackBufferHeight = swapChainDesc.Height;
	m_screenBackBufferAspectRatio = (m_screenBackBufferHeight ? (static_cast<float>(m_screenBackBufferWidth) / m_screenBackBufferHeight) : 0);

	m_screenBackBufferIndex = m_screenSwapChain->GetCurrentBackBufferIndex();

	for (UINT i = 0; i < m_screenBackBufferCount; i++)
	{
		ThrowIfFailed(m_screenSwapChain->GetBuffer(i, IID_PPV_ARGS(m_screenBackBuffers[i].ReleaseAndGetAddressOf())));
	}
}

void Podo::ResetViewPort()
{
	m_screenViewPort.TopLeftX	= 0.0f;
	m_screenViewPort.TopLeftY	= 0.0f;
	m_screenViewPort.Width		= FLOAT(m_screenBackBufferWidth);
	m_screenViewPort.Height		= FLOAT(m_screenBackBufferHeight);
	m_screenViewPort.MinDepth	= 0.0f;
	m_screenViewPort.MaxDepth	= 1.0f;
}

void Podo::ResetScissorRectangle()
{
	m_screenScissorRectangle.left	= 0;
	m_screenScissorRectangle.top	= 0;
	m_screenScissorRectangle.right	= m_screenBackBufferWidth;
	m_screenScissorRectangle.bottom	= m_screenBackBufferHeight;
}

void Podo::ResetDepthStencilBuffer()
{
	D3D12_RESOURCE_DESC depthStencilBufferDesc = {};
	depthStencilBufferDesc.Dimension			= D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilBufferDesc.Alignment			= 0;
	depthStencilBufferDesc.Width				= m_screenBackBufferWidth;
	depthStencilBufferDesc.Height				= m_screenBackBufferHeight;
	depthStencilBufferDesc.DepthOrArraySize		= 1;
	depthStencilBufferDesc.MipLevels			= 1;
	depthStencilBufferDesc.Format				= m_screenDepthStencilBufferFormat;
	depthStencilBufferDesc.SampleDesc.Count		= 1;
	depthStencilBufferDesc.SampleDesc.Quality	= 0;
	depthStencilBufferDesc.Layout				= D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilBufferDesc.Flags				= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = m_screenDepthStencilBufferFormat;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	D3D12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed
	(
		m_device->CreateCommittedResource
		(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&depthStencilBufferDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&clearValue,
			IID_PPV_ARGS(m_screenDepthStencilBuffer.ReleaseAndGetAddressOf())
		)
	);
}

void Podo::ResetRTV()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandleRTV = m_descriptorHeapRTVStartHandleCPU;
	for (UINT i = 0; i < m_screenBackBufferCount; i++)
	{
		m_device->CreateRenderTargetView(m_screenBackBuffers[i].Get(), nullptr, cpuHandleRTV.Offset(i, m_descriptorHeapRTVIncrementSize));
	}
}

void Podo::ResetDSV()
{
	m_device->CreateDepthStencilView(m_screenDepthStencilBuffer.Get(), nullptr, m_descriptorHeapDSVStartHandleCPU);
}

void Podo::ResetAssets()
{
	m_workloadAssets.clear();

	Asset box;

	box.vertices =
	{
		{DirectX::XMFLOAT3{-0.5f, -0.5f, -0.5f}},
		{DirectX::XMFLOAT3{-0.5f, +0.5f, -0.5f}},
		{DirectX::XMFLOAT3{+0.5f, +0.5f, -0.5f}},
		{DirectX::XMFLOAT3{+0.5f, -0.5f, -0.5f}},

		{DirectX::XMFLOAT3{-0.5f, -0.5f, +0.5f}},
		{DirectX::XMFLOAT3{-0.5f, +0.5f, +0.5f}},
		{DirectX::XMFLOAT3{+0.5f, +0.5f, +0.5f}},
		{DirectX::XMFLOAT3{+0.5f, -0.5f, +0.5f}}
	};

	box.indices =
	{
		0, 1, 2,  0, 2, 3,
		4, 6, 5,  4, 7, 6,
		4, 5, 1,  4, 1, 0,
		3, 2, 6,  3, 6, 7,
		1, 5, 6,  1, 6, 2,
		4, 0, 3,  4, 3, 7
	};

	DirectX::ResourceUploadBatch resourceUpload(m_device.Get());
	resourceUpload.Begin();
	{
		box.CreateBuffers(m_device.Get(), resourceUpload);
	}
	auto uploadFinished = resourceUpload.End(m_commandQueue.Get());

	box.UpdateBufferViews();

	uploadFinished.wait();

	m_workloadAssets["Box"] = std::move(box);
}

void Podo::ResetObjects()
{
	m_workloadObjects.clear();

	{
		Object boxObject;
		boxObject.asset = &m_workloadAssets.at("Box");

		DirectX::XMVECTOR position	= DirectX::XMVectorSet(0.0f, 0.0f, 3.0f, 1.0f);
		DirectX::XMVECTOR direction = DirectX::XMVectorSet(1.0f, 0.0f, 1.0f, 0.0f);
		DirectX::XMVECTOR up		= DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		boxObject.SetWorldSpace(position, direction, up);
		boxObject.CreateConstantBuffer(m_device.Get());

		m_workloadObjects["HorizontalBoxObject"] = std::move(boxObject);
	}

	{
		Object verticalBoxObject;
		verticalBoxObject.asset = &m_workloadAssets.at("Box");

		DirectX::XMVECTOR position	= DirectX::XMVectorSet(2.0f, 0.0f, 5.0f, 1.0f);
		DirectX::XMVECTOR direction	= DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
		DirectX::XMVECTOR up		= DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		verticalBoxObject.SetWorldSpace(position, direction, up);
		verticalBoxObject.CreateConstantBuffer(m_device.Get());

		m_workloadObjects["VerticalBoxObject"] = std::move(verticalBoxObject);
	}
}

void Podo::ResetCBVSRVUAV()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_descriptorHeapCBVSRVUAVStartHandleCPUForRender;
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_descriptorHeapCBVSRVUAVStartHandleGPUForRender;

	for (auto& [name, object] : m_workloadObjects)
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation	= object.constantBuffer->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes		= static_cast<UINT>(object.constantBuffer->GetDesc().Width);

		m_device->CreateConstantBufferView(&cbvDesc, cpuHandle);

		object.constantBufferViewGPUHandle = gpuHandle;

		cpuHandle.Offset(1, m_descriptorHeapCBVSRVUAVIncrementSize);
		gpuHandle.Offset(1, m_descriptorHeapCBVSRVUAVIncrementSize);
	}
}

void Podo::ResetRootSignature()
{
	CD3DX12_ROOT_PARAMETER rootParameter[ROOT_PARAMETER_COUNT] = {};

	CD3DX12_DESCRIPTOR_RANGE objectConstantTable[1] = {};

	UINT objectDescriptorNum = 1;
	objectConstantTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, objectDescriptorNum, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND);

	rootParameter[OBJECT_CONSTANT].InitAsDescriptorTable(1, objectConstantTable);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc
	(
		ROOT_PARAMETER_COUNT,
		rootParameter,
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature
	(
		&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf()
	);

	if (errorBlob != nullptr)
	{
		OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
	}

	ThrowIfFailed(hr);

	ThrowIfFailed
	(
		m_device->CreateRootSignature
		(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(m_basicRootSignature.ReleaseAndGetAddressOf())
		)
	);
}

void Podo::ResetPipelineStateObject()
{
	D3D12_INPUT_ELEMENT_DESC inputElementDesc[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = { inputElementDesc, _countof(inputElementDesc) };

	ComPtr<ID3DBlob> vs;
	ComPtr<ID3DBlob> ps;
	ThrowIfFailed(D3DReadFileToBlob(L"VertexShader.cso", &vs));
	ThrowIfFailed(D3DReadFileToBlob(L"PixelShader.cso", &ps));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateObjectDesc	= {};
	pipelineStateObjectDesc.InputLayout							= inputLayoutDesc;
	pipelineStateObjectDesc.pRootSignature						= m_basicRootSignature.Get();
	pipelineStateObjectDesc.VS									= CD3DX12_SHADER_BYTECODE(vs.Get());
	pipelineStateObjectDesc.PS									= CD3DX12_SHADER_BYTECODE(ps.Get());
	pipelineStateObjectDesc.RasterizerState						= CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pipelineStateObjectDesc.BlendState							= CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pipelineStateObjectDesc.DepthStencilState					= CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pipelineStateObjectDesc.SampleMask							= UINT_MAX;
	pipelineStateObjectDesc.PrimitiveTopologyType				= D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipelineStateObjectDesc.NumRenderTargets					= 1;
	pipelineStateObjectDesc.RTVFormats[0]						= m_optionHDR.IsActive() ? m_screenBackBufferFormatHDR : m_screenBackBufferFormatSDR;
	pipelineStateObjectDesc.SampleDesc.Count					= 1;
	pipelineStateObjectDesc.SampleDesc.Quality					= 0;
	pipelineStateObjectDesc.DSVFormat							= m_screenDepthStencilBufferFormat;
	
	ThrowIfFailed(m_device->CreateGraphicsPipelineState(&pipelineStateObjectDesc, IID_PPV_ARGS(m_basicPipelineStateObject.ReleaseAndGetAddressOf())));
}

void Podo::ResetImGui()
{
	CloseImGui();

	m_imGuiDescriptorHeapAllocator.Create(m_device.Get(), m_descriptorHeapCBVSRVUAV.Get(), m_descriptorHeapCBVSRVUAVCapacityForGUI);

	ImGui_ImplDX12_InitInfo initInfo = {};
	initInfo.Device = m_device.Get();
	initInfo.CommandQueue = m_commandQueue.Get();
	initInfo.NumFramesInFlight = 1;
	initInfo.RTVFormat = m_optionHDR.IsActive() ? m_screenBackBufferFormatHDR : m_screenBackBufferFormatSDR;

	initInfo.SrvDescriptorHeap = m_descriptorHeapCBVSRVUAV.Get();
	initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* pOutCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
		{
			return m_imGuiDescriptorHeapAllocator.Alloc(pOutCpuHandle, outGpuHandle);
		};
	initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
		{
			return m_imGuiDescriptorHeapAllocator.Free(cpuHandle, gpuHandle);
		};

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui_ImplWin32_Init(m_hWnd);
	ImGui_ImplDX12_Init(&initInfo);

	ImGui::StyleColorsClassic();

	ImGuiStyle& style = ImGui::GetStyle();
	float imGuiScale = 2.0f * m_optionGUI.GetMasterScale();
	style.ScaleAllSizes(imGuiScale);
	style.FontScaleDpi = imGuiScale;

	ImVec4* colors = style.Colors;
	colors[ImGuiCol_Text]				= ImVec4(0.78f, 0.74f, 0.63f, 1.0f);
	colors[ImGuiCol_TextDisabled]		= ImVec4(0.38f, 0.38f, 0.34f, 1.0f);
	colors[ImGuiCol_FrameBg]			= ImVec4(0.105f, 0.110f, 0.100f, 1.0f);
	colors[ImGuiCol_FrameBgHovered]		= ImVec4(0.185f, 0.175f, 0.130f, 1.0f);
	colors[ImGuiCol_FrameBgActive]		= ImVec4(0.260f, 0.230f, 0.140f, 1.0f);
	colors[ImGuiCol_CheckMark]			= ImVec4(0.82f, 0.68f, 0.32f, 1.0f);
	colors[ImGuiCol_SliderGrab]			= ImVec4(0.58f, 0.50f, 0.30f, 1.0f);
	colors[ImGuiCol_SliderGrabActive]	= ImVec4(0.82f, 0.66f, 0.28f, 1.0f);
	colors[ImGuiCol_WindowBg]			= ImVec4(0.075f, 0.078f, 0.075f, 1.0f);
	colors[ImGuiCol_TitleBg]			= ImVec4(0.115f, 0.115f, 0.105f, 1.0f);
	colors[ImGuiCol_TitleBgActive]		= ImVec4(0.180f, 0.170f, 0.145f, 1.0f);
	colors[ImGuiCol_TitleBgCollapsed]	= ImVec4(0.060f, 0.062f, 0.060f, 1.0f);
	colors[ImGuiCol_Button]				= ImVec4(0.210f, 0.215f, 0.200f, 1.0f);
	colors[ImGuiCol_ButtonHovered]		= ImVec4(0.340f, 0.320f, 0.250f, 1.0f);
	colors[ImGuiCol_ButtonActive]		= ImVec4(0.470f, 0.380f, 0.180f, 1.0f);
	colors[ImGuiCol_Border]				= ImVec4(0.30f, 0.28f, 0.23f, 0.55f);
	colors[ImGuiCol_Separator]			= ImVec4(0.34f, 0.31f, 0.25f, 0.65f);
	colors[ImGuiCol_SeparatorHovered]	= ImVec4(0.45f, 0.38f, 0.34f, 0.78f);
	colors[ImGuiCol_SeparatorActive]	= ImVec4(0.55f, 0.45f, 0.43f, 0.90f);

	m_imGuiInitialized = true;
}