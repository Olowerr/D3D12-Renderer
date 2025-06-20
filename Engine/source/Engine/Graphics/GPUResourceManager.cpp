#include "GPUResourceManager.h"

#include "glm/gtc/integer.hpp"

namespace Okay
{
	void GPUResourceManager::initialize(ID3D12Device* pDevice, DescriptorHeapStore& descriptorHeapStore)
	{
		m_pDevice = pDevice;
		m_pDescriptorHeapStore = &descriptorHeapStore;

		m_heapStore.initialize(m_pDevice, 100'000'000, 1'000'000);
	}

	void GPUResourceManager::shutdown()
	{
		for (Resource& resource : m_resources)
		{
			D3D12_RELEASE(resource.pDXResource);
		}
		m_resources.clear();

		m_heapStore.shutdown();

		m_pDevice = nullptr;
	}

	Allocation GPUResourceManager::createTexture(const TextureDescription& textureDesc, const void* pData, CommandContext* pUploadContext)
	{
		bool isDepth = textureDesc.flags & OKAY_TEXTURE_FLAG_DEPTH;

		D3D12_CLEAR_VALUE clearValue = {};
		clearValue.Format = textureDesc.format;
		D3D12_CLEAR_VALUE* pClearValue = nullptr;

		// how to improve :thonk:
		if (isDepth)
		{
			pClearValue = &clearValue;
			clearValue.DepthStencil.Depth = 1.f;
			clearValue.DepthStencil.Stencil = 0;
			clearValue.Format = DXGI_FORMAT_D32_FLOAT; // Assuming depth uses some single 32bit format
		}
		else if (textureDesc.flags & OKAY_TEXTURE_FLAG_RENDER)
		{
			pClearValue = &clearValue;
			clearValue.Color[0] = 0.f;
			clearValue.Color[1] = 0.f;
			clearValue.Color[2] = 0.f;
			clearValue.Color[3] = 0.f;
		}

		// Ensure we don't try to create more mipmaps than possible given the width & height
		uint32_t mipLevels = glm::min(textureDesc.mipLevels, (uint16_t)(glm::log2(glm::max(textureDesc.width, textureDesc.height)) + 1));

		Resource& resource = m_resources.emplace_back();
		resource.pDXResource = m_heapStore.requestResource(D3D12_HEAP_TYPE_DEFAULT, textureDesc.width,textureDesc.height, mipLevels, textureDesc.arraySize, textureDesc.format, pClearValue, isDepth);

		D3D12_RESOURCE_DESC desc = resource.pDXResource->GetDesc();
		D3D12_RESOURCE_ALLOCATION_INFO resourceAllocationInfo = m_pDevice->GetResourceAllocationInfo(0, 1, &desc);

		resource.nextAppendOffset = resourceAllocationInfo.SizeInBytes;
		resource.maxSize = resourceAllocationInfo.SizeInBytes;

		if (pData)
		{
			updateTexture(resource.pDXResource, (uint8_t*)pData, *pUploadContext);

			if (textureDesc.flags & OKAY_TEXTURE_FLAG_SHADER_READ && mipLevels == 1)
			{
				pUploadContext->transitionResource(resource.pDXResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			}
		}

		Allocation allocation = {};
		allocation.resourceHandle = ResourceHandle(m_resources.size() - 1);
		allocation.pDXResource = resource.pDXResource;
		allocation.resourceOffset = 0;
		allocation.elementSize = resource.maxSize;
		allocation.numElements = 1;

		return allocation;
	}

	Resource GPUResourceManager::createResource(D3D12_HEAP_TYPE heapType, uint64_t size)
	{
		size = alignAddress64(size, BUFFER_DATA_ALIGNMENT);

		Resource& resource = m_resources.emplace_back();

		resource.handle = ResourceHandle(m_resources.size() - 1);
		resource.pDXResource = m_heapStore.requestResource(heapType, size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, nullptr, false);
		resource.heapType = heapType;

		resource.nextAppendOffset = 0;
		resource.maxSize = size;

		return resource;
	}

	Allocation GPUResourceManager::allocateInto(Resource resource, uint64_t offset, uint64_t elementSize, uint32_t numElements, const void* pData, RingBuffer* pUploadBuffer, CommandContext* pUploadContext)
	{
		validateResourceHandle(resource.handle);
		Resource& resourceRef = m_resources[resource.handle];

		if (offset == 0 && elementSize == 0 && numElements == 1)
		{
			elementSize = resourceRef.maxSize;
		}
		else if (offset == OKAY_RESOURCE_APPEND)
		{
			offset = resourceRef.nextAppendOffset;
			OKAY_ASSERT(offset + elementSize * numElements <= resourceRef.maxSize);
		}

		resourceRef.nextAppendOffset += alignAddress64(elementSize * numElements, BUFFER_DATA_ALIGNMENT);

		Allocation allocation = {};
		allocation.resourceHandle = resourceRef.handle;
		allocation.pDXResource = resourceRef.pDXResource;
		allocation.resourceOffset = alignAddress64(offset, BUFFER_DATA_ALIGNMENT);
		allocation.elementSize = elementSize;
		allocation.numElements = numElements;

		if (pData)
		{
			updateBuffer(allocation, pData, pUploadBuffer, pUploadContext);
		}

		return allocation;
	}

	void GPUResourceManager::updateBuffer(const Allocation& allocation, const void* pData, RingBuffer* pUploadBuffer, CommandContext* pUploadContext)
	{
		validateResourceHandle(allocation.resourceHandle);
		Resource& resource = m_resources[allocation.resourceHandle];

		if (resource.heapType == D3D12_HEAP_TYPE_DEFAULT)
		{
			OKAY_ASSERT(pUploadBuffer);
			OKAY_ASSERT(pUploadContext);
			updateBufferUpload(*pUploadBuffer, *pUploadContext, resource.pDXResource, allocation.resourceOffset, allocation.elementSize * allocation.numElements, pData);
		}
		else
		{
			updateBufferDirect(resource.pDXResource, allocation.resourceOffset, allocation.elementSize * allocation.numElements, pData);
		}
	}

	D3D12_GPU_VIRTUAL_ADDRESS GPUResourceManager::getVirtualAddress(const Allocation& allocation)
	{
		return allocation.pDXResource->GetGPUVirtualAddress() + allocation.resourceOffset;
	}

	DescriptorDesc GPUResourceManager::createDescriptorDesc(const Allocation& allocation, DescriptorType type, bool nullDesc)
	{
		OKAY_ASSERT(type != OKAY_DESCRIPTOR_TYPE_NONE);

		DescriptorDesc desc = {};
		desc.type = type;
		desc.nullDesc = nullDesc;
		desc.pDXResource = allocation.pDXResource;

		if (nullDesc)
		{
			return desc;
		}

		switch (type)
		{
		case OKAY_DESCRIPTOR_TYPE_SRV: // Assuming it's a structured buffer, textures should pass in nulLDesc as true
			desc.srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			desc.srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			desc.srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

			desc.srvDesc.Buffer.FirstElement = 0; // might not be...or?
			desc.srvDesc.Buffer.NumElements = allocation.numElements;
			desc.srvDesc.Buffer.StructureByteStride = (uint32_t)allocation.elementSize;
			desc.srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
			break;

		case OKAY_DESCRIPTOR_TYPE_CBV:
			desc.cbvDesc.BufferLocation = allocation.pDXResource->GetGPUVirtualAddress() + allocation.resourceOffset;
			desc.cbvDesc.SizeInBytes = alignAddress32((uint32_t)allocation.elementSize * allocation.numElements, BUFFER_DATA_ALIGNMENT);
			break;

		case OKAY_DESCRIPTOR_TYPE_UAV:
			OKAY_ASSERT(false); // TODO: Implement UAVs
			break;

		case OKAY_DESCRIPTOR_TYPE_RTV:
			OKAY_ASSERT(false); // RTVs should set nullDesc to true, atleast for now
			break;

		case OKAY_DESCRIPTOR_TYPE_DSV:
			OKAY_ASSERT(false); // DSVs should set nullDesc to true, atleast for now
			break;
		}

		return desc;
	}

	void GPUResourceManager::generateMipMaps(ID3D12CommandQueue* pCommandQueue)
	{
		/*
			In DirectX 12 we need to generate the content of the mipMaps manually.

			To do this, we create an intermediate UAV texture that is the max size of all textures.
			For each texture we then copy it's content to the intermediate, loop through the mip maps and down sample using a CS.
			After all mipmaps have been proccessed, we copy the contents of the intermediate to the original texture.

			Essentially:

			* Create intermediate UAV texture based on maxTextureDims & largestMipDepth

			For each texture:
				* Copy contents into intermediate
				* Bind SRV to mip0 of intermediate
				
				For each mip (starting at mip1):
					* Create UAV to the mip
					* Dispatch CS that will sample mip i - 1 & write to the UAV
				
				* Copy intermediate to original texture
			
			Note:
			The reason that i - 1 is used as the index for both the SRV & UAV is because we want to sample the parent mip,
			and the descriptor table starts at mip1. Meaning in the CS that uavMips[0] refers to mip1.
		*/


		uint16_t totalMipMaps = 0;
		uint16_t largetMipDepth = 0;
		glm::uvec2 largestTextureDims = glm::uvec2(0, 0);
		for (Resource& resource : m_resources)
		{
			D3D12_RESOURCE_DESC textureDesc = resource.pDXResource->GetDesc();

			if (textureDesc.MipLevels == 1)
			{
				continue;
			}

			largestTextureDims = glm::max(largestTextureDims, glm::uvec2(textureDesc.Width, textureDesc.Height));
			largetMipDepth = glm::max(largetMipDepth, textureDesc.MipLevels);

			totalMipMaps += textureDesc.MipLevels;
		}

		if (!totalMipMaps)
		{
			return;
		}


		D3D12_RESOURCE_DESC intermediateTextureDesc = m_resources[0].pDXResource->GetDesc();
		intermediateTextureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		intermediateTextureDesc.Alignment = RESOURCE_PLACEMENT_ALIGNMENT;
		intermediateTextureDesc.Width = largestTextureDims.x;
		intermediateTextureDesc.Height = largestTextureDims.y;
		intermediateTextureDesc.DepthOrArraySize = 1;
		intermediateTextureDesc.MipLevels = largetMipDepth;
		intermediateTextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		intermediateTextureDesc.SampleDesc.Count = 1;
		intermediateTextureDesc.SampleDesc.Quality = 0;
		intermediateTextureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		intermediateTextureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		D3D12_HEAP_PROPERTIES intermediateHeapProperties{};
		intermediateHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		intermediateHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		intermediateHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		intermediateHeapProperties.CreationNodeMask = 0;
		intermediateHeapProperties.VisibleNodeMask = 0;

		ID3D12Resource* pDXIntermediateTexture = nullptr;
		DX_CHECK(m_pDevice->CreateCommittedResource(&intermediateHeapProperties, D3D12_HEAP_FLAG_NONE, &intermediateTextureDesc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&pDXIntermediateTexture)));


		CommandContext computeContext;
		computeContext.initialize(m_pDevice, pCommandQueue);
		ID3D12GraphicsCommandList* pGraphicsComputeList = computeContext.getCommandList();

		RingBuffer ringBuffer;
		ringBuffer.initialize(m_pDevice, RESOURCE_PLACEMENT_ALIGNMENT * 2);
		ringBuffer.map();

		ID3D12RootSignature* pMipMapRootSignature = createMipMapRootSignature();
		ID3D12PipelineState* pMipMapPSO = createMipMapPSO(pMipMapRootSignature);

		pGraphicsComputeList->SetComputeRootSignature(pMipMapRootSignature);
		pGraphicsComputeList->SetPipelineState(pMipMapPSO);


		DescriptorHeapHandle descHeapHandle = m_pDescriptorHeapStore->createDescriptorHeap(totalMipMaps, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
		ID3D12DescriptorHeap* pDescriptorHeap = m_pDescriptorHeapStore->getDXDescriptorHeap(descHeapHandle);

		pGraphicsComputeList->SetDescriptorHeaps(1, &pDescriptorHeap);

		for (Resource& resource : m_resources)
		{
			D3D12_RESOURCE_DESC textureDesc = resource.pDXResource->GetDesc();

			if (textureDesc.MipLevels == 1)
			{
				continue;
			}


			computeContext.transitionResource(resource.pDXResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
			computeContext.transitionResource(pDXIntermediateTexture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

			copyDifferentTextures(pGraphicsComputeList, pDXIntermediateTexture, resource.pDXResource, (uint32_t)textureDesc.Width, textureDesc.Height);

			computeContext.transitionResource(pDXIntermediateTexture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);


			DescriptorDesc textureSRVDesc = {};
			textureSRVDesc.type = OKAY_DESCRIPTOR_TYPE_SRV;
			textureSRVDesc.pDXResource = pDXIntermediateTexture;

			Descriptor srvDescriptor = m_pDescriptorHeapStore->allocateDescriptors(descHeapHandle, OKAY_DESCRIPTOR_APPEND, &textureSRVDesc, 1);
			pGraphicsComputeList->SetComputeRootDescriptorTable(0, srvDescriptor.gpuHandle);


			for (uint32_t i = 1; i < textureDesc.MipLevels; i++)
			{
				DescriptorDesc currentMipUAVDesc = {};
				currentMipUAVDesc.type = OKAY_DESCRIPTOR_TYPE_UAV;
				currentMipUAVDesc.pDXResource = pDXIntermediateTexture;

				currentMipUAVDesc.nullDesc = false;
				currentMipUAVDesc.uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
				currentMipUAVDesc.uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				currentMipUAVDesc.uavDesc.Texture2D.MipSlice = i;
				currentMipUAVDesc.uavDesc.Texture2D.PlaneSlice = 0;

				m_pDescriptorHeapStore->allocateDescriptors(descHeapHandle, OKAY_DESCRIPTOR_APPEND, &currentMipUAVDesc, 1);
				computeContext.transitionSubresource(pDXIntermediateTexture, i, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);


				float uavIdxAndSrvMip = (float)i - 1;
				D3D12_GPU_VIRTUAL_ADDRESS cbAddress = ringBuffer.allocateMapped(&uavIdxAndSrvMip, sizeof(float));
				pGraphicsComputeList->SetComputeRootConstantBufferView(1, cbAddress);


				uint32_t mipWidth = (uint32_t)textureDesc.Width / (uint32_t)glm::pow(2, i);
				uint32_t mipHeight = textureDesc.Height / (uint32_t)glm::pow(2, i);
				pGraphicsComputeList->Dispatch(mipWidth / 16 + 1, mipHeight / 16 + 1, 1);

				computeContext.transitionSubresource(pDXIntermediateTexture, i, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			}

			computeContext.transitionResource(pDXIntermediateTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
			computeContext.transitionResource(resource.pDXResource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

			copyDifferentTextures(pGraphicsComputeList, resource.pDXResource, pDXIntermediateTexture, (uint32_t)textureDesc.Width, textureDesc.Height);

			computeContext.transitionResource(resource.pDXResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		}

		computeContext.flush();
		computeContext.shutdown();
		ringBuffer.shutdown();
		
		D3D12_RELEASE(pDXIntermediateTexture);
		D3D12_RELEASE(pMipMapRootSignature);
		D3D12_RELEASE(pMipMapPSO);
	}

	void GPUResourceManager::updateBufferUpload(RingBuffer& ringBuffer, CommandContext& uploadContext, ID3D12Resource* pDXResource, uint64_t resourceOffset, uint64_t byteSize, const void* pData)
	{
		uint64_t ringBufferOffset = ringBuffer.getOffset();

		ringBuffer.allocateMapped(pData, byteSize);

		ID3D12GraphicsCommandList* pCommandList = uploadContext.getCommandList();
		pCommandList->CopyBufferRegion(pDXResource, resourceOffset, ringBuffer.getDXResource(), ringBufferOffset, byteSize);
	}

	void GPUResourceManager::updateBufferDirect(ID3D12Resource* pDXResource, uint64_t resourceOffset, uint64_t byteSize, const void* pData)
	{
		D3D12_RANGE readRange = { 0, 0 };

		uint8_t* pMappedData = nullptr;
		DX_CHECK(pDXResource->Map(0, &readRange, (void**)&pMappedData));

		memcpy(pMappedData + resourceOffset, pData, byteSize);

		pDXResource->Unmap(0, nullptr);
	}

	void GPUResourceManager::updateTexture(ID3D12Resource* pDXResource, uint8_t* pData, CommandContext& uploadContext)
	{
		D3D12_RESOURCE_DESC textureDesc = pDXResource->GetDesc();
		textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footPrint = {};
		uint64_t rowSizeInBytes = 0;
		m_pDevice->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footPrint, nullptr, &rowSizeInBytes, nullptr);


		D3D12_RESOURCE_ALLOCATION_INFO resourceAllocationInfo = m_pDevice->GetResourceAllocationInfo(0, 1, &textureDesc);

		RingBuffer textureUploadBuffer;
		textureUploadBuffer.initialize(m_pDevice, resourceAllocationInfo.SizeInBytes);

		uint8_t* pMappedData = textureUploadBuffer.map();

		for (uint32_t i = 0; i < textureDesc.Height; i++)
		{
			memcpy(pMappedData + i * footPrint.Footprint.RowPitch, pData + i * rowSizeInBytes, rowSizeInBytes);
		}

		textureUploadBuffer.unmap();


		D3D12_TEXTURE_COPY_LOCATION copyDst{};
		copyDst.pResource = pDXResource;
		copyDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		copyDst.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION copySrc{};
		copySrc.pResource = textureUploadBuffer.getDXResource();
		copySrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		copySrc.PlacedFootprint = footPrint;
		copySrc.PlacedFootprint.Offset = 0;

		ID3D12GraphicsCommandList* pCommandList = uploadContext.getCommandList();
		pCommandList->CopyTextureRegion(&copyDst, 0, 0, 0, &copySrc, nullptr);

		uploadContext.flush();
		textureUploadBuffer.shutdown();
	}

	void GPUResourceManager::validateResourceHandle(ResourceHandle handle)
	{
		OKAY_ASSERT(handle < (ResourceHandle)m_resources.size());
	}

	ID3D12RootSignature* GPUResourceManager::createMipMapRootSignature()
	{
		D3D12_DESCRIPTOR_RANGE descriptorRanges[2] = {};
		descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		descriptorRanges[0].NumDescriptors = 16;
		descriptorRanges[0].OffsetInDescriptorsFromTableStart = 1;
		descriptorRanges[0].BaseShaderRegister = 0;
		descriptorRanges[0].RegisterSpace = 0;

		descriptorRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		descriptorRanges[1].NumDescriptors = 1;
		descriptorRanges[1].OffsetInDescriptorsFromTableStart = 0;
		descriptorRanges[1].BaseShaderRegister = 0;
		descriptorRanges[1].RegisterSpace = 0;


		D3D12_ROOT_PARAMETER rootParams[2] = {};
		rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParams[0].DescriptorTable.NumDescriptorRanges = _countof(descriptorRanges);
		rootParams[0].DescriptorTable.pDescriptorRanges = descriptorRanges;

		rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParams[1].Descriptor.ShaderRegister = 0;
		rootParams[1].Descriptor.RegisterSpace = 0;


		D3D12_STATIC_SAMPLER_DESC defaultSampler = createDefaultStaticPointSamplerDesc();
		defaultSampler.Filter = D3D12_FILTER_ANISOTROPIC;
		defaultSampler.MaxAnisotropy = D3D12_MAX_MAXANISOTROPY;
		defaultSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
		rootDesc.NumParameters = _countof(rootParams);
		rootDesc.pParameters = rootParams;
		rootDesc.NumStaticSamplers = 1;
		rootDesc.pStaticSamplers = &defaultSampler;
		rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		return createRootSignature(m_pDevice, rootDesc);
	}

	ID3D12PipelineState* GPUResourceManager::createMipMapPSO(ID3D12RootSignature* pRootSignature)
	{
		ID3DBlob* pShaderBlob = nullptr;

		D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
		computeDesc.pRootSignature = pRootSignature;
		computeDesc.CS = compileShader(SHADER_PATH / "MipMapGenerationCS.hlsl", "cs_5_1", &pShaderBlob);
		computeDesc.NodeMask = 0;
		computeDesc.CachedPSO.pCachedBlob = nullptr;
		computeDesc.CachedPSO.CachedBlobSizeInBytes = 0;
		computeDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		ID3D12PipelineState* pMipMapPSO = nullptr;
		DX_CHECK(m_pDevice->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(&pMipMapPSO)));

		D3D12_RELEASE(pShaderBlob);

		return pMipMapPSO;
	}
}
