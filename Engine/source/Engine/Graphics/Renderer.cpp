#include "Renderer.h"

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

		createSwapChain(pFactory, window);

		m_gpuResourceManager.initialize(m_pDevice, m_commandContext);
		m_descriptorHeapStore.initialize(m_pDevice, 10);

		fetchBackBuffersAndDSV();

		m_cbvSrvUavDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_rtvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_dsvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		createPSO();

		struct Vertex
		{
			glm::vec3 position;
			glm::vec4 colour;
		};

		Vertex verticies[] = 
		{
			{ .position = glm::vec3(-0.5f, -0.5f, 0.f), .colour = glm::vec4(1.f, 0.f, 0.f, 1.f) },
			{ .position = glm::vec3(0.5f, -0.5f, 0.0f), .colour = glm::vec4(0.f, 0.f, 1.f, 1.f) },
			{ .position = glm::vec3(0.f, 0.5f, 0.f), .colour = glm::vec4(0.f, 1.f, 0.f, 1.f) },
		};

		uint32_t indicies[] = 
		{
			0, 2, 1
		};


		ResourceHandle triangleBufferRH = m_gpuResourceManager.createResource(D3D12_HEAP_TYPE_DEFAULT, 1'000);
		m_triangleColourAH = m_gpuResourceManager.addConstantBuffer(triangleBufferRH, 16, nullptr);
		m_vertexBufferAH = m_gpuResourceManager.addStructuredBuffer(triangleBufferRH, sizeof(verticies[0]), _countof(verticies), &verticies);

		ResourceHandle indexBufferRH = m_gpuResourceManager.createResource(D3D12_HEAP_TYPE_DEFAULT, sizeof(indicies));
		m_indexBufferAH = m_gpuResourceManager.addStructuredBuffer(indexBufferRH, sizeof(indicies[0]), _countof(indicies), &indicies);
		m_commandContext.transitionResource(m_gpuResourceManager.getDXResource(indexBufferRH), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
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

		D3D12_RELEASE(m_pRootSignature);
		D3D12_RELEASE(m_pPSO);
	}
	
	void Renderer::render(const Scene& scene)
	{
		glm::vec4 colour = glm::vec4(rand() / (float)RAND_MAX, rand() / (float)RAND_MAX, rand() / (float)RAND_MAX, 1.f);
		m_gpuResourceManager.updateBuffer(m_triangleColourAH, &colour);

		preRender();
		renderScene(scene);
		postRender();
	}

	void Renderer::preRender()
	{
		m_currentBackBuffer = (m_currentBackBuffer + 1) % NUM_BACKBUFFERS;
		m_commandContext.transitionResource(m_backBuffers[m_currentBackBuffer], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		D3D12_CPU_DESCRIPTOR_HANDLE dsvCPUDescriptorHandle = m_descriptorHeapStore.getCPUHandle(m_dsvDescriptor);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvCPUDescriptorHandle = m_descriptorHeapStore.getCPUHandle(m_rtvFirstDescriptor);
		rtvCPUDescriptorHandle.ptr += (uint64_t)m_currentBackBuffer * m_rtvDescriptorSize;

		ID3D12GraphicsCommandList* pCommandList = m_commandContext.getCommandList();

		float testClearColor[4] = { 0.9f, 0.5f, 0.4f, 1.f };
		pCommandList->ClearRenderTargetView(rtvCPUDescriptorHandle, testClearColor, 0, nullptr);
		pCommandList->ClearDepthStencilView(dsvCPUDescriptorHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

		D3D12_INDEX_BUFFER_VIEW ibView = {};
		ibView.BufferLocation = m_gpuResourceManager.getVirtualAddress(m_indexBufferAH);
		ibView.SizeInBytes = m_gpuResourceManager.getTotalSize(m_indexBufferAH);
		ibView.Format = DXGI_FORMAT_R32_UINT;

		pCommandList->IASetIndexBuffer(&ibView);
		pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		pCommandList->RSSetViewports(1, &m_viewport);
		pCommandList->RSSetScissorRects(1, &m_scissorRect);

		pCommandList->OMSetRenderTargets(1, &rtvCPUDescriptorHandle, false, &dsvCPUDescriptorHandle);
	}

	void Renderer::renderScene(const Scene& scene)
	{
		// temp
		ID3D12GraphicsCommandList* pCommandList = m_commandContext.getCommandList();
		
		pCommandList->SetGraphicsRootSignature(m_pRootSignature);
		pCommandList->SetPipelineState(m_pPSO);

		pCommandList->SetGraphicsRootConstantBufferView(0, m_gpuResourceManager.getVirtualAddress(m_triangleColourAH));
		pCommandList->SetGraphicsRootShaderResourceView(1, m_gpuResourceManager.getVirtualAddress(m_vertexBufferAH));

		pCommandList->DrawIndexedInstanced(3, 1, 0, 0, 0);
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

		Renderer::logAdapterInfo(pAdapter);

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
		ResourceHandle dsHandle = m_gpuResourceManager.createTexture((uint32_t)resourceDesc.Width, resourceDesc.Height, DXGI_FORMAT_D32_FLOAT, OKAY_TEXTURE_FLAG_DEPTH, nullptr);

		DescriptorDesc dsvDesc = m_gpuResourceManager.createDescriptorDesc(dsHandle, OKAY_DESCRIPTOR_TYPE_DSV, true);
		m_dsvDescriptor = m_descriptorHeapStore.createDescriptors(1, &dsvDesc, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		ID3D12Resource* pDsvResource = m_gpuResourceManager.getDXResource(dsHandle);
		m_commandContext.transitionResource(pDsvResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_DEPTH_WRITE);

		// Find viewport & scissor rect
		D3D12_RESOURCE_DESC backBufferDesc = m_backBuffers[0]->GetDesc();

		m_viewport.TopLeftX = 0.f;
		m_viewport.TopLeftY = 0.f;
		m_viewport.Width = (float)backBufferDesc.Width;
		m_viewport.Height = (float)backBufferDesc.Height;
		m_viewport.MinDepth = 0;
		m_viewport.MaxDepth = 1;

		m_scissorRect.left = 0;
		m_scissorRect.top = 0;
		m_scissorRect.right = (LONG)backBufferDesc.Width;
		m_scissorRect.bottom = (LONG)backBufferDesc.Height;
	}

	void Renderer::logAdapterInfo(IDXGIAdapter* pAdapter)
	{
		DXGI_ADAPTER_DESC adapterDesc{};
		pAdapter->GetDesc(&adapterDesc);

		printf("-- GPU INFO --\n");
		printf("Name: %ws\n", adapterDesc.Description);
		printf("Dedicated Video Memory: %.2f GB\n", adapterDesc.DedicatedVideoMemory / 1'000'000'000.f);
		printf("Dedicated System Memory: %.2f GB\n", adapterDesc.DedicatedSystemMemory / 1'000'000'000.f);
		printf("Shared System Memory: %.2f GB\n\n", adapterDesc.SharedSystemMemory / 1'000'000'000.f);
	}

	D3D12_SHADER_BYTECODE Renderer::compileShader(std::filesystem::path path, std::string_view version, ID3DBlob** pShaderBlob)
	{
		ID3DBlob* pErrorBlob = nullptr;

#ifdef _DEBUG
		uint32_t flags1 = D3DCOMPILE_DEBUG;
#else
		uint32_t flags1 = D3DCOMPILE_OPTIMIZATION_LEVEL2;;
#endif

		HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, nullptr, "main", version.data(), flags1, 0, pShaderBlob, &pErrorBlob);
		if (FAILED(hr))
		{
			const char* pErrorMsg = pErrorBlob ? (const char*)pErrorBlob->GetBufferPointer() : "No errors produced, file might have been found.";

			printf("Shader Compilation failed\n    Path: %ls\n    Error: %s\n", path.c_str(), pErrorMsg);
			OKAY_ASSERT(false);
		}

		D3D12_RELEASE(pErrorBlob);

		D3D12_SHADER_BYTECODE shaderByteCode{};
		shaderByteCode.pShaderBytecode = (*pShaderBlob)->GetBufferPointer();
		shaderByteCode.BytecodeLength = (*pShaderBlob)->GetBufferSize();

		return shaderByteCode;
	}

	void Renderer::createPSO()
	{
		D3D12_ROOT_PARAMETER rootParams[2] = {};

		rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParams[0].Descriptor.ShaderRegister = 0;
		rootParams[0].Descriptor.RegisterSpace = 0;
		rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		rootParams[1].Descriptor.ShaderRegister = 0;
		rootParams[1].Descriptor.RegisterSpace = 0;
		rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		D3D12_ROOT_SIGNATURE_DESC rootDesc{};
		rootDesc.NumParameters = _countof(rootParams);
		rootDesc.pParameters = rootParams;
		rootDesc.NumStaticSamplers = 0;
		rootDesc.pStaticSamplers = nullptr;
		rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		ID3DBlob* pRootBlob = nullptr;
		ID3DBlob* pErrorBlob = nullptr;

		HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &pRootBlob, &pErrorBlob);
		if (FAILED(hr))
		{
			printf("Failed to serialize root signature: %s\n", (char*)pErrorBlob->GetBufferPointer());
			OKAY_ASSERT(false);
		}

		DX_CHECK(m_pDevice->CreateRootSignature(0, pRootBlob->GetBufferPointer(), pRootBlob->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature)));

		ID3DBlob* pVsShaderBlob = nullptr;
		ID3DBlob* pPsShaderBlob = nullptr;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
		pipelineDesc.pRootSignature = m_pRootSignature;

		pipelineDesc.VS = Renderer::compileShader(SHADER_PATH / "VertexShader.hlsl", "vs_5_1", &pVsShaderBlob);
		pipelineDesc.PS = Renderer::compileShader(SHADER_PATH / "PixelShader.hlsl", "ps_5_1", &pPsShaderBlob);

		pipelineDesc.StreamOutput.pSODeclaration = nullptr;
		pipelineDesc.StreamOutput.NumEntries = 0;
		pipelineDesc.StreamOutput.pBufferStrides = nullptr;
		pipelineDesc.StreamOutput.NumStrides = 0;
		pipelineDesc.StreamOutput.RasterizedStream = 0;

		pipelineDesc.BlendState.AlphaToCoverageEnable = false;
		pipelineDesc.BlendState.IndependentBlendEnable = false;
		pipelineDesc.NumRenderTargets = 1;

		pipelineDesc.BlendState.RenderTarget[0].BlendEnable = false;
		pipelineDesc.BlendState.RenderTarget[0].LogicOpEnable = false;
		pipelineDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		pipelineDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
		pipelineDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		pipelineDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		pipelineDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		pipelineDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		pipelineDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
		pipelineDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		pipelineDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

		pipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		pipelineDesc.RasterizerState.FrontCounterClockwise = false;
		pipelineDesc.RasterizerState.DepthBias = 0;
		pipelineDesc.RasterizerState.DepthBiasClamp = 0.0f;
		pipelineDesc.RasterizerState.SlopeScaledDepthBias = 0.0f;
		pipelineDesc.RasterizerState.DepthClipEnable = true;
		pipelineDesc.RasterizerState.MultisampleEnable = false;
		pipelineDesc.RasterizerState.AntialiasedLineEnable = false;
		pipelineDesc.RasterizerState.ForcedSampleCount = 0;
		pipelineDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		pipelineDesc.DepthStencilState.DepthEnable = true;
		pipelineDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		pipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		pipelineDesc.DepthStencilState.StencilEnable = false;
		pipelineDesc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
		pipelineDesc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
		pipelineDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		pipelineDesc.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		pipelineDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		pipelineDesc.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		pipelineDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		pipelineDesc.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		pipelineDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		pipelineDesc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

		pipelineDesc.InputLayout.pInputElementDescs = nullptr;
		pipelineDesc.InputLayout.NumElements = 0;
		pipelineDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;

		pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		pipelineDesc.NumRenderTargets = 1;
		pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pipelineDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

		pipelineDesc.SampleDesc.Count = 1;
		pipelineDesc.SampleDesc.Quality = 0;

		pipelineDesc.NodeMask = 0;
		
		pipelineDesc.CachedPSO.CachedBlobSizeInBytes = 0;
		pipelineDesc.CachedPSO.pCachedBlob = nullptr;

		pipelineDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		DX_CHECK(m_pDevice->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&m_pPSO)));

		D3D12_RELEASE(pVsShaderBlob);
		D3D12_RELEASE(pPsShaderBlob);
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
