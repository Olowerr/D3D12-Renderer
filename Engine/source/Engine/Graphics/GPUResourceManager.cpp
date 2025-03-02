#include "GPUResourceManager.h"

namespace Okay
{
	void GPUResourceManager::initialize(ID3D12Device* pDevice, CommandContext& commandContext)
	{
		m_pDevice = pDevice;
		m_pCommandContext = &commandContext;

		m_staticHeapStore.initialize(m_pDevice, D3D12_HEAP_TYPE_DEFAULT, 100'000'000);
		m_dynamicHeapStore.initialize(m_pDevice, D3D12_HEAP_TYPE_UPLOAD, 1'000'000);

		createUploadResource(&m_pUploadBuffer, RESOURCE_PLACEMENT_ALIGNMENT);
		m_uploadHeapCurrentSize = 0;
		m_uploadHeapMaxSize = RESOURCE_PLACEMENT_ALIGNMENT;
	}

	void GPUResourceManager::shutdown()
	{
		for (Resource& resource : m_staticResources)
			D3D12_RELEASE(resource.pDXResource);

		for (Resource& resource : m_dynamicResources)
			D3D12_RELEASE(resource.pDXResource);

		m_staticResources.clear();
		m_dynamicResources.clear();

		m_allocations.clear();

		m_staticHeapStore.shutdown();
		m_dynamicHeapStore.shutdown();

		D3D12_RELEASE(m_pUploadBuffer);

		m_pDevice = nullptr;
	}

	ResourceHandle GPUResourceManager::addTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t flags, void* pData)
	{
		OKAY_ASSERT(width);
		OKAY_ASSERT(height);
		OKAY_ASSERT(TextureFlags(flags) != OKAY_TEXTURE_FLAG_NONE);
		OKAY_ASSERT(pData);

		uint16_t resourceIdx = (uint16_t)m_staticResources.size();
		uint16_t allocationIdx = (uint16_t)m_allocations.size();

		Resource& resource = m_staticResources.emplace_back();
		ResourceAllocation& allocation = m_allocations.emplace_back();
		allocation.resourceOffset = 0;

		bool isDepth = CHECK_BIT(flags, OKAY_TEXTURE_FLAG_DEPTH);
		resource.pDXResource = m_staticHeapStore.requestResource(width, height, 1, DXGI_FORMAT(format), isDepth);

		D3D12_RESOURCE_DESC desc = resource.pDXResource->GetDesc();
		m_pDevice->GetCopyableFootprints(&desc, 0, 1, 0, nullptr, nullptr, nullptr, &resource.usedSize);

		updateTexture(resource.pDXResource, (unsigned char*)pData);

		return generateHandle(resourceIdx, allocationIdx, OKAY_BUFFER_USAGE_STATIC);
	}

	ResourceHandle GPUResourceManager::addConstantBuffer(BufferUsage usage, uint32_t byteSize, void* pData)
	{
		OKAY_ASSERT(usage != OKAY_BUFFER_USAGE_NONE);
		OKAY_ASSERT(byteSize);

		return addBufferInternal(usage, byteSize, 1, pData);
	}

	ResourceHandle GPUResourceManager::addStructuredBuffer(BufferUsage usage, uint32_t elementSize, uint32_t elementCount, void* pData)
	{
		OKAY_ASSERT(usage != OKAY_BUFFER_USAGE_NONE);
		OKAY_ASSERT(elementSize);
		OKAY_ASSERT(elementCount);

		return addBufferInternal(usage, elementSize, elementCount, pData);
	}

	ResourceHandle GPUResourceManager::addVertexBuffer(uint32_t vertexSize, uint32_t numVerticies, void* pData)
	{
		OKAY_ASSERT(vertexSize);
		OKAY_ASSERT(numVerticies);
		OKAY_ASSERT(pData);

		return addBufferInternal(OKAY_BUFFER_USAGE_STATIC, vertexSize, numVerticies, pData);
	}

	ResourceHandle GPUResourceManager::addIndexBuffer(uint32_t numIndicies, void* pData)
	{
		OKAY_ASSERT(numIndicies);
		OKAY_ASSERT(pData);

		return addBufferInternal(OKAY_BUFFER_USAGE_STATIC, sizeof(uint32_t), numIndicies, pData);
	}

	void GPUResourceManager::updateBuffer(ResourceHandle handle, void* pData)
	{
		Resource* pResource = nullptr;
		ResourceAllocation* pAllocation = nullptr;
		BufferUsage* pUsage = nullptr;

		decodeHandle(handle, &pResource, &pAllocation, &pUsage);

		updateBufferInternal(*pResource, *pAllocation, *pUsage, pData);
	}

	ID3D12Resource* GPUResourceManager::getDXResource(ResourceHandle handle)
	{
		Resource* pResource = nullptr;
		decodeHandle(handle, &pResource, nullptr, nullptr);

		return pResource->pDXResource;
	}

	D3D12_GPU_VIRTUAL_ADDRESS GPUResourceManager::getVirtualAddress(ResourceHandle handle)
	{
		Resource* pResource = nullptr;
		ResourceAllocation* pAllocation = nullptr;

		decodeHandle(handle, &pResource, &pAllocation, nullptr);

		return pResource->pDXResource->GetGPUVirtualAddress() + pAllocation->resourceOffset;
	}

	const ResourceAllocation& GPUResourceManager::getAllocation(ResourceHandle handle)
	{
		ResourceAllocation* pAllocation = nullptr;
		decodeHandle(handle, nullptr, &pAllocation, nullptr);

		return *pAllocation;
	}

	uint32_t GPUResourceManager::getTotalSize(ResourceHandle handle)
	{
		ResourceAllocation* pAllocation = nullptr;
		decodeHandle(handle, nullptr, &pAllocation, nullptr);

		return pAllocation->elementSize * pAllocation->numElements;
	}

	DescriptorDesc GPUResourceManager::createDescriptorDesc(ResourceHandle handle, DescriptorType type, bool nullDesc)
	{
		// handle asserted in decodeHandle
		OKAY_ASSERT(type != OKAY_DESCRIPTOR_TYPE_NONE);

		Resource* pResource = nullptr;
		ResourceAllocation* pAllocation = nullptr;
		BufferUsage* pUsage = nullptr;

		decodeHandle(handle, &pResource, &pAllocation, &pUsage);

		DescriptorDesc desc = {};

		desc.type = type;
		desc.nullDesc = nullDesc;
		desc.pDXResource = pResource->pDXResource;

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
			desc.srvDesc.Buffer.NumElements = pAllocation->numElements;
			desc.srvDesc.Buffer.StructureByteStride = pAllocation->elementSize;
			desc.srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
			break;

		case OKAY_DESCRIPTOR_TYPE_CBV:
			desc.cbvDesc.BufferLocation = pResource->pDXResource->GetGPUVirtualAddress() + pAllocation->resourceOffset;
			desc.cbvDesc.SizeInBytes = alignAddress32(pAllocation->elementSize * pAllocation->numElements, BUFFER_DATA_ALIGNMENT);
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

	void GPUResourceManager::updateBufferInternal(const Resource& resource, const ResourceAllocation& allocation, BufferUsage usage, void* pData)
	{
		if (usage == OKAY_BUFFER_USAGE_STATIC)
		{
			updateBufferUpload(resource.pDXResource, allocation.resourceOffset, allocation.elementSize * allocation.numElements, pData);
		}
		else
		{
			updateBufferDirect(resource.pDXResource, allocation.resourceOffset, allocation.elementSize * allocation.numElements, pData);
		}
	}

	void GPUResourceManager::updateBufferUpload(ID3D12Resource* pDXResource, uint64_t resourceOffset, uint32_t byteSize, void* pData)
	{
		if (m_uploadHeapCurrentSize + byteSize > m_uploadHeapMaxSize)
		{
			resizeUploadBuffer(uint64_t((m_uploadHeapCurrentSize + byteSize) * 1.2f));
		}

		D3D12_RANGE readRange = { 0, 0 };

		unsigned char* pMappedData = nullptr;
		DX_CHECK(m_pUploadBuffer->Map(0, &readRange, (void**)&pMappedData));

		memcpy(pMappedData + m_uploadHeapCurrentSize, pData, byteSize);

		m_pUploadBuffer->Unmap(0, nullptr);

		ID3D12GraphicsCommandList* pCommandList = m_pCommandContext->getCommandList();
		pCommandList->CopyBufferRegion(pDXResource, resourceOffset, m_pUploadBuffer, m_uploadHeapCurrentSize, byteSize);
		
		m_uploadHeapCurrentSize = alignAddress64(m_uploadHeapCurrentSize + (uint64_t)byteSize, BUFFER_DATA_ALIGNMENT);
	}

	void GPUResourceManager::updateBufferDirect(ID3D12Resource* pDXResource, uint64_t resourceOffset, uint32_t byteSize, void* pData)
	{
		D3D12_RANGE readRange = { 0, 0 };

		unsigned char* pMappedData = nullptr;
		DX_CHECK(pDXResource->Map(0, &readRange, (void**)&pMappedData));

		memcpy(pMappedData + resourceOffset, pData, byteSize);

		pDXResource->Unmap(0, nullptr);
	}

	void GPUResourceManager::updateTexture(ID3D12Resource* pDXResource, unsigned char* pData)
	{
		/*
			Currently works by:
				1. Create new upload heap
				2. CPU copy texture data into upload heap
				3. Copy upload heap to target resource

			Can create the upload heap at initialization, but it might not be used a lot after initialization
			and might need to resize

			Maybe can also try copying from CPU directly into target?
			Since now we're creating a new upload heap everytime, so maybe worth ?
		*/

		D3D12_RESOURCE_DESC textureDesc = pDXResource->GetDesc();

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footPrint{};
		uint64_t rowSizeInBytes = 0;
		uint64_t resourceTotalSize = 0;
		m_pDevice->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footPrint, nullptr, &rowSizeInBytes, &resourceTotalSize);

		ID3D12Resource* pUploadResource = nullptr;
		createUploadResource(&pUploadResource, resourceTotalSize);

		D3D12_RANGE range = { 0, 0 };

		unsigned char* pMappedData = nullptr;
		pUploadResource->Map(0, &range, (void**)&pMappedData);

		for (uint32_t i = 0; i < textureDesc.Height; i++)
		{
			memcpy(pMappedData + i * rowSizeInBytes, pData + i * textureDesc.Width * 4, textureDesc.Width * 4);
		}

		pUploadResource->Unmap(0, nullptr);

		D3D12_TEXTURE_COPY_LOCATION copyDst{};
		copyDst.pResource = pDXResource;
		copyDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		copyDst.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION copySrc{};
		copySrc.pResource = pUploadResource;
		copySrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		copySrc.PlacedFootprint = footPrint;
		copySrc.PlacedFootprint.Offset = 0;

		ID3D12GraphicsCommandList* pCommandList = m_pCommandContext->getCommandList();
		pCommandList->CopyTextureRegion(&copyDst, 0, 0, 0, &copySrc, nullptr);

		m_pCommandContext->flush();

		D3D12_RELEASE(pUploadResource);
	}

	ResourceHandle GPUResourceManager::generateHandle(uint16_t resourceIndex, uint16_t allocationIndex, BufferUsage usage)
	{
		ResourceHandle handle = 0;
		uint8_t* pHandle = (uint8_t*)&handle;

		/* Not sure which version i prefer :thonk:
		pHandle[HANDLE_RESOURCE_IDX_OFFSET] = resourceIndex;
		pHandle[HANDLE_ALLOCATION_IDX_OFFSET] = resourceIndex;
		pHandle[HANDLE_USAGE_OFFSET] = resourceIndex;
		*/

		*(pHandle + HANDLE_RESOURCE_IDX_OFFSET) = (uint8_t)resourceIndex;
		*(pHandle + HANDLE_ALLOCATION_IDX_OFFSET) = (uint8_t)allocationIndex;
		*(pHandle + HANDLE_USAGE_OFFSET) = (uint8_t)usage;

		return handle;
	}

	void GPUResourceManager::decodeHandle(ResourceHandle handle, Resource** ppOutResource, ResourceAllocation** ppOutAllocation, BufferUsage** ppOutUsage)
	{
		uint8_t* pHandle = (uint8_t*)&handle;

		uint16_t resourceIndex = *(pHandle + HANDLE_RESOURCE_IDX_OFFSET);
		uint16_t allocationIndex = *(pHandle + HANDLE_ALLOCATION_IDX_OFFSET);
		BufferUsage usage = (BufferUsage) *(pHandle + HANDLE_USAGE_OFFSET);

		validateDecodedHandle(resourceIndex, allocationIndex, usage);

		std::vector<Resource>& resourceList = getResourceList(usage);

		if (ppOutResource)
			*ppOutResource = &resourceList[resourceIndex];
		
		if (ppOutAllocation)
			*ppOutAllocation = &m_allocations[allocationIndex];
		
		if (ppOutUsage)
			*ppOutUsage = &usage;
	}

	void GPUResourceManager::validateDecodedHandle(uint16_t resourceIndex, uint16_t allocationIndex, BufferUsage usage)
	{
		OKAY_ASSERT(usage == OKAY_BUFFER_USAGE_STATIC || usage == OKAY_BUFFER_USAGE_DYNAMIC);

		OKAY_ASSERT(allocationIndex < (uint16_t)m_allocations.size());

		OKAY_ASSERT(resourceIndex < (uint16_t)getResourceList(usage).size());
	}

	ResourceHandle GPUResourceManager::addBufferInternal(BufferUsage usage, uint32_t elementSize, uint32_t elementCount, void* pData)
	{
		std::vector<Resource>& resourceList = getResourceList(usage);

		uint32_t totalGPUByteSize = alignAddress32(elementSize * elementCount, BUFFER_DATA_ALIGNMENT);

		uint16_t resourceIdx = INVALID_UINT16;
		uint16_t allocationIdx = (uint16_t)m_allocations.size();

		for (uint16_t i = 0; i < (uint16_t)resourceList.size(); i++)
		{
			uint64_t resourceTotalSize = resourceList[i].pDXResource->GetDesc().Width;
			if ((uint64_t)totalGPUByteSize < resourceTotalSize - resourceList[i].usedSize)
			{
				resourceIdx = i;
				break;
			}
		}

		if (resourceIdx == INVALID_UINT16)
		{
			resourceIdx = (uint16_t)resourceList.size();
			resourceList.emplace_back();

			uint64_t allocationSize = std::max((uint64_t)totalGPUByteSize, (uint64_t)RESOURCE_PLACEMENT_ALIGNMENT);
			HeapStore& heapStore = getHeapStore(usage);

			resourceList[resourceIdx].pDXResource = heapStore.requestResource(allocationSize, 1, 1, DXGI_FORMAT_UNKNOWN, false);
			resourceList[resourceIdx].usedSize = 0;
		}

		ResourceAllocation& allocation = m_allocations.emplace_back();
		allocation.resourceOffset = resourceList[resourceIdx].usedSize;
		allocation.numElements = elementCount;
		allocation.elementSize = elementSize;

		if (pData)
		{
			updateBufferInternal(resourceList[resourceIdx], allocation, usage, pData);
		}

		resourceList[resourceIdx].usedSize += totalGPUByteSize;

		return generateHandle(resourceIdx, allocationIdx, OKAY_BUFFER_USAGE_STATIC);
	}

	D3D12_HEAP_TYPE GPUResourceManager::getHeapType(BufferUsage usage)
	{
		OKAY_ASSERT(usage != OKAY_BUFFER_USAGE_NONE);

		switch (usage)
		{
		case OKAY_BUFFER_USAGE_STATIC:
			return D3D12_HEAP_TYPE_DEFAULT;

		case OKAY_BUFFER_USAGE_DYNAMIC:
			return D3D12_HEAP_TYPE_UPLOAD;
		}

		OKAY_ASSERT(false); // There is no invalid value for the D3D12_HEAP_TYPE enum ):
		return D3D12_HEAP_TYPE_DEFAULT;
	}

	HeapStore& GPUResourceManager::getHeapStore(BufferUsage usage)
	{
		return usage == OKAY_BUFFER_USAGE_STATIC ? m_staticHeapStore : m_dynamicHeapStore;
	}

	std::vector<Resource>& GPUResourceManager::getResourceList(BufferUsage usage)
	{
		return usage == OKAY_BUFFER_USAGE_STATIC ? m_staticResources : m_dynamicResources;
	}

	void GPUResourceManager::createUploadResource(ID3D12Resource** ppDXResource, uint64_t byteSize)
	{
		D3D12_RESOURCE_DESC uploadDesc{};
		uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		uploadDesc.Alignment = RESOURCE_PLACEMENT_ALIGNMENT;
		uploadDesc.Width = byteSize;
		uploadDesc.Height = 1;
		uploadDesc.DepthOrArraySize = 1;
		uploadDesc.MipLevels = 1;
		uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
		uploadDesc.SampleDesc.Count = 1;
		uploadDesc.SampleDesc.Quality = 0;
		uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProperties.CreationNodeMask = 0;
		heapProperties.VisibleNodeMask = 0;

		DX_CHECK(m_pDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(ppDXResource)));
	}

	void GPUResourceManager::resizeUploadBuffer(uint64_t newSize)
	{
		m_pCommandContext->flush();

		D3D12_RELEASE(m_pUploadBuffer);

		createUploadResource(&m_pUploadBuffer, newSize);
		m_uploadHeapCurrentSize = 0;
		m_uploadHeapMaxSize = newSize;
	}
}
