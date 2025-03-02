#include "Renderer.h"

#include <dxgidebug.h>

namespace Okay
{
	/* TODO:
	* ? Merge all the "add...Buffer" functions in GPUResourceManager into addBuffer
		* Takes in same parameters as current addStructuredBuffer()
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
		m_gpuResourceManager.initialize(m_pDevice, m_commandContext);
		m_cbvSrvUavDescriptorHeapStore.initialize(m_pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 10);
		m_rtvDescriptorHeapStore.initialize(m_pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 10);
		m_dsvDescriptorHeapStore.initialize(m_pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 10);

		ResourceHandle handle = m_gpuResourceManager.addConstantBuffer(OKAY_BUFFER_USAGE_STATIC, 30000, nullptr);
		ResourceHandle handle1 = m_gpuResourceManager.addConstantBuffer(OKAY_BUFFER_USAGE_STATIC, 30000, nullptr);
		ResourceHandle handle2 = m_gpuResourceManager.addStructuredBuffer(OKAY_BUFFER_USAGE_STATIC, 24, 2000, nullptr);

		uint32_t width = 4096;
		uint32_t height = 4096;
		unsigned char* pTestTextureData = new unsigned char[width * height * 4] {};
		for (uint32_t y = 0; y < height; y++)
		{
			for (uint32_t x = 0; x < width; x++)
			{
				unsigned char r = (x / width) * 255;
				unsigned char g = (y / height) * 255;
				unsigned char b = 0; // (x / height) * 255;
				unsigned char a = 0;

				pTestTextureData[(y * width + x) * 4 + 0] = r;
				pTestTextureData[(y * width + x) * 4 + 1] = g;
				pTestTextureData[(y * width + x) * 4 + 2] = b;
				pTestTextureData[(y * width + x) * 4 + 3] = a;
			}
		}

		ResourceHandle textureHandle1 = m_gpuResourceManager.addTexture(width / 2, height / 2, DXGI_FORMAT_R8G8B8A8_UNORM, OKAY_TEXTURE_FLAG_SHADER_READ, pTestTextureData);
		ResourceHandle textureHandle2 = m_gpuResourceManager.addTexture(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, OKAY_TEXTURE_FLAG_SHADER_READ, pTestTextureData);

		m_gpuResourceManager.updateBuffer(handle1, pTestTextureData);

		DescriptorDesc descriptorDescs[5] =
		{
			m_gpuResourceManager.createDescriptorDesc(textureHandle1, OKAY_DESCRIPTOR_TYPE_SRV, true),
			m_gpuResourceManager.createDescriptorDesc(textureHandle2, OKAY_DESCRIPTOR_TYPE_SRV, true),
			m_gpuResourceManager.createDescriptorDesc(handle, OKAY_DESCRIPTOR_TYPE_CBV, false),
			m_gpuResourceManager.createDescriptorDesc(handle2, OKAY_DESCRIPTOR_TYPE_SRV, false),
			m_gpuResourceManager.createDescriptorDesc(handle1, OKAY_DESCRIPTOR_TYPE_CBV, false),
		};

		uint32_t tableStartSlot = m_cbvSrvUavDescriptorHeapStore.createDescriptors(5, descriptorDescs);
	}
	
	void Renderer::shutdown()
	{
		m_commandContext.shutdown();
		m_gpuResourceManager.shutdown();

		m_cbvSrvUavDescriptorHeapStore.shutdown();
		m_rtvDescriptorHeapStore.shutdown();
		m_dsvDescriptorHeapStore.shutdown();

		D3D12_RELEASE(m_pDevice);
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