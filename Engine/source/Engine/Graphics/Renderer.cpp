#include "Renderer.h"

#include <dxgidebug.h>

namespace Okay
{
	/* TODO:
	* ? Merge all the "add...Buffer" functions in GPUResourceManager into addBuffer
		* Takes in same parameters as current addStructuredBuffer()
	* ? Merge the three heap stores into 1
		* I'm not sure i like having 3 just chilling there?
		* but in a way kinda nice, but :thonk:

	*/

	void Renderer::initialize(const Window& window)
	{
#ifndef NDEBUG
		enableDebugLayer();
		enableGPUBasedValidation();
#endif

		IDXGIFactory* pFactory = nullptr;
		DX_CHECK(CreateDXGIFactory(IID_PPV_ARGS(&pFactory)));

		createDevice(pFactory);
		m_commandContext.initialize(m_pDevice);

		createSwapChain(pFactory, window);

		m_gpuResourceManager.initialize(m_pDevice, m_commandContext);
		m_descriptorHeapStore.initialize(m_pDevice, 10);

		fetchBackBuffersAndDSV();

		m_cbvSrvUavDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_rtvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_dsvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	}
	
	void Renderer::shutdown()
	{
		m_commandContext.shutdown();
		m_gpuResourceManager.shutdown();

		m_descriptorHeapStore.shutdown();

		for (uint32_t i = 0; i < NUM_BACKBUFFERS; i++)
			D3D12_RELEASE(m_backBuffers[i]);

		D3D12_RELEASE(m_pSwapChain);
		D3D12_RELEASE(m_pDevice);
	}
	
	void Renderer::render(const Scene& scene)
	{
		preRender();
		renderScene(scene);
		postRender();
	}

	void Renderer::preRender()
	{
		m_currentBackBuffer = (m_currentBackBuffer + 1) % NUM_BACKBUFFERS;
		m_commandContext.transitionResource(m_backBuffers[m_currentBackBuffer], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		D3D12_CPU_DESCRIPTOR_HANDLE dsvDescriptorHandle = m_descriptorHeapStore.getCPUHandle(m_dsvDescriptor);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvDescriptorCPUHandle = m_descriptorHeapStore.getCPUHandle(m_rtvFirstDescriptor);
		rtvDescriptorCPUHandle.ptr += (uint64_t)m_currentBackBuffer * m_rtvDescriptorSize;

		ID3D12GraphicsCommandList* pCommandList = m_commandContext.getCommandList();

		float testClearColor[4] = { 0.9f, 0.5f, 0.4f, 1.f };
		pCommandList->ClearRenderTargetView(rtvDescriptorCPUHandle, testClearColor, 0, nullptr);
		pCommandList->ClearDepthStencilView(dsvDescriptorHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
	}

	void Renderer::renderScene(const Scene& scene)
	{
	}

	void Renderer::postRender()
	{
		m_commandContext.transitionResource(m_backBuffers[m_currentBackBuffer], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

		m_commandContext.execute();
		m_pSwapChain->Present(1, 0);

		m_commandContext.wait();
		m_commandContext.reset();
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

		DX_CHECK(pFactory2->CreateSwapChainForHwnd(m_commandContext.getCommandQueue(), window.getHWND(), &swapChainDesc, nullptr, nullptr, &m_pSwapChain));

		D3D12_RELEASE(pFactory2);
	}

	void Renderer::fetchBackBuffersAndDSV()
	{
		DescriptorDesc backBufferRTVs[NUM_BACKBUFFERS] = {};

		for (uint32_t i = 0; i < NUM_BACKBUFFERS; i++)
		{
			DX_CHECK(m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])));

			backBufferRTVs[i].type = OKAY_DESCRIPTOR_TYPE_RTV;
			backBufferRTVs[i].pDXResource = m_backBuffers[i];
		}

		m_rtvFirstDescriptor = m_descriptorHeapStore.createDescriptors(NUM_BACKBUFFERS, backBufferRTVs, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// Create depth stencil texture & descriptor
		D3D12_RESOURCE_DESC resourceDesc = m_backBuffers[0]->GetDesc();
		ResourceHandle dsHandle = m_gpuResourceManager.addTexture((uint32_t)resourceDesc.Width, resourceDesc.Height, DXGI_FORMAT_D32_FLOAT, OKAY_TEXTURE_FLAG_DEPTH, nullptr);

		DescriptorDesc dsvDesc = m_gpuResourceManager.createDescriptorDesc(dsHandle, OKAY_DESCRIPTOR_TYPE_DSV, true);
		m_dsvDescriptor = m_descriptorHeapStore.createDescriptors(1, &dsvDesc, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		ID3D12Resource* pDsvResource = m_gpuResourceManager.getDXResource(dsHandle);
		m_commandContext.transitionResource(pDsvResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	}

	void Renderer::enableDebugLayer()
	{
		ID3D12Debug* pDebugController = nullptr;
		DX_CHECK(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController)));

		pDebugController->EnableDebugLayer();
		D3D12_RELEASE(pDebugController);
	}

	void Renderer::enableGPUBasedValidation()
	{
		ID3D12Debug1* pDebugController = nullptr;
		DX_CHECK(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebugController)));

		pDebugController->SetEnableGPUBasedValidation(true);
		D3D12_RELEASE(pDebugController);
	}
}