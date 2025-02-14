#include "Renderer.h"

#include <dxgidebug.h>

namespace Okay
{
	Renderer::~Renderer()
	{
		shutdown();
	}
	
	void Renderer::initialize(const Window& window)
	{
		IDXGIFactory* pFactory = nullptr;
		DX_CHECK(CreateDXGIFactory(IID_PPV_ARGS(&pFactory)));

		createDevice(pFactory);
		createCommandList();

		m_gpuResourceManager.initialize(m_pDevice);
	}
	
	void Renderer::shutdown()
	{
		D3D12_RELEASE(m_pDevice);
		D3D12_RELEASE(m_pCommandQueue);
		D3D12_RELEASE(m_pCommandAllocator);
		D3D12_RELEASE(m_pCommandList);

		m_gpuResourceManager.shutdown();
	}
	
	void Renderer::render(const Scene& scene)
	{
	}

	void Renderer::createDevice(IDXGIFactory* pFactory)
	{
		IDXGIAdapter* pAdapter = nullptr;
		uint32_t adapterIndex = 0;

		while (!pAdapter)
		{
			DX_CHECK(pFactory->EnumAdapters(adapterIndex, &pAdapter));

			HRESULT hr = D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device*), nullptr);
			if (FAILED(hr))
			{
				D3D12_RELEASE(pAdapter);
			}

			adapterIndex++;
		}

		D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_pDevice));
		D3D12_RELEASE(pAdapter);
	}

	void Renderer::createCommandList()
	{
		D3D12_COMMAND_QUEUE_DESC desc{};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 0;

		DX_CHECK(m_pDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_pCommandQueue)));
		
		DX_CHECK(m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_pCommandAllocator)));

		DX_CHECK(m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pCommandAllocator, nullptr, IID_PPV_ARGS(&m_pCommandList)));
	}

	void Renderer::createSwapChain(IDXGIFactory* pFactory, const Window& window)
	{
		IDXGIFactory2* pFactory2 = nullptr;
		DX_CHECK(pFactory->QueryInterface(IID_PPV_ARGS(&pFactory2)));

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
		swapChainDesc.Width = 0;
		swapChainDesc.Height = 0;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.Stereo = FALSE;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = NUM_BACKBUFFERS;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		swapChainDesc.Flags = 0;

		DX_CHECK(pFactory2->CreateSwapChainForHwnd(m_pDevice, window.getHWND(), &swapChainDesc, nullptr, nullptr, &m_pSwapChain));
	}

}