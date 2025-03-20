#include "GPUResourceManager.h"

// Describes the "slots" of uint16_t used within a ResourceHandle
// Currently assuming each part needs 16 bits
constexpr uint8_t HANDLE_RESOURCE_IDX_SLOT = 0;
constexpr uint8_t HANDLE_ALLOCATION_IDX_SLOT = 1;

namespace Okay
{
	void GPUResourceManager::initialize(ID3D12Device* pDevice, CommandContext& commandContext)
	{
		m_pDevice = pDevice;
		m_pCommandContext = &commandContext;

		m_heapStore.initialize(m_pDevice, 100'000'000, 1'000'000);

		createUploadResource(&m_pUploadBuffer, RESOURCE_PLACEMENT_ALIGNMENT);
		m_uploadHeapCurrentSize = 0;
		m_uploadHeapMaxSize = RESOURCE_PLACEMENT_ALIGNMENT;
	}

	void GPUResourceManager::shutdown()
	{
		for (Resource& resource : m_resources)
			D3D12_RELEASE(resource.pDXResource);

		m_resources.clear();

		m_allocations.clear();

		m_heapStore.shutdown();

		D3D12_RELEASE(m_pUploadBuffer);

		m_pDevice = nullptr;
	}

	AllocationHandle GPUResourceManager::createTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t flags, const void* pData)
	{
		OKAY_ASSERT(width);
		OKAY_ASSERT(height);
		OKAY_ASSERT(TextureFlags(flags) != OKAY_TEXTURE_FLAG_NONE);

		uint16_t resourceIdx = (uint16_t)m_resources.size();
		uint16_t allocationIdx = (uint16_t)m_allocations.size();

		bool isDepth = flags & OKAY_TEXTURE_FLAG_DEPTH;

		D3D12_CLEAR_VALUE clearValue = {};
		clearValue.Format = format;
		D3D12_CLEAR_VALUE* pClearValue = nullptr;

		// how to improve :thonk:
		if (isDepth)
		{
			pClearValue = &clearValue;
			clearValue.DepthStencil.Depth = 1.f;
			clearValue.DepthStencil.Stencil = 0;
		}
		else if (flags & OKAY_TEXTURE_FLAG_RENDER)
		{
			pClearValue = &clearValue;
			clearValue.Color[0] = 0.f;
			clearValue.Color[1] = 0.f;
			clearValue.Color[2] = 0.f;
			clearValue.Color[3] = 0.f;
		}

		Resource& resource = m_resources.emplace_back();
		resource.pDXResource = m_heapStore.requestResource(D3D12_HEAP_TYPE_DEFAULT, width, height, 1, DXGI_FORMAT(format), pClearValue, isDepth);

		D3D12_RESOURCE_DESC desc = resource.pDXResource->GetDesc();
		D3D12_RESOURCE_ALLOCATION_INFO resourceAllocationInfo = m_pDevice->GetResourceAllocationInfo(0, 1, &desc);

		resource.usedSize = resourceAllocationInfo.SizeInBytes;
		resource.maxSize = resourceAllocationInfo.SizeInBytes;

		ResourceAllocation& allocation = m_allocations.emplace_back();
		allocation.elementSize = (uint32_t)resource.usedSize;
		allocation.numElements = 1;
		allocation.resourceOffset = 0;

		if (pData)
		{
			updateTexture(resource.pDXResource, (unsigned char*)pData);
		}

		return generateAllocationHandle(resourceIdx, allocationIdx);
	}

	ResourceHandle GPUResourceManager::createResource(D3D12_HEAP_TYPE heapType, uint64_t size)
	{
		size = alignAddress64(size, BUFFER_DATA_ALIGNMENT);

		ResourceHandle handle = (ResourceHandle)m_resources.size();

		Resource& resource = m_resources.emplace_back();

		resource.pDXResource = m_heapStore.requestResource(heapType, size, 1, 1, DXGI_FORMAT_UNKNOWN, nullptr, false);
		resource.heapType = heapType;

		resource.usedSize = 0;
		resource.maxSize = size;

		return handle;
	}

	AllocationHandle GPUResourceManager::addConstantBuffer(ResourceHandle resourceHandle, uint32_t byteSize, const void* pData)
	{
		if (byteSize == 0)
		{
			validateResourceHandle(resourceHandle);
			byteSize = (uint32_t)m_resources[resourceHandle].maxSize;
		}

		return addBufferInternal(resourceHandle, byteSize, 1, pData);
	}

	AllocationHandle GPUResourceManager::addStructuredBuffer(ResourceHandle resourceHandle, uint32_t elementSize, uint32_t elementCount, const void* pData)
	{
		OKAY_ASSERT(elementSize);
		OKAY_ASSERT(elementCount);

		return addBufferInternal(resourceHandle, elementSize, elementCount, pData);
	}

	void GPUResourceManager::updateBuffer(AllocationHandle handle, const void* pData)
	{
		Resource* pResource = nullptr;
		ResourceAllocation* pAllocation = nullptr;

		decodeAllocationHandle(handle, &pResource, &pAllocation);

		updateBufferInternal(*pResource, *pAllocation, pData);
	}

	ID3D12Resource* GPUResourceManager::getDXResource(AllocationHandle handle)
	{
		Resource* pResource = nullptr;
		decodeAllocationHandle(handle, &pResource, nullptr);

		return pResource->pDXResource;
	}

	D3D12_GPU_VIRTUAL_ADDRESS GPUResourceManager::getVirtualAddress(AllocationHandle handle)
	{
		Resource* pResource = nullptr;
		ResourceAllocation* pAllocation = nullptr;

		decodeAllocationHandle(handle, &pResource, &pAllocation);

		return pResource->pDXResource->GetGPUVirtualAddress() + pAllocation->resourceOffset;
	}

	void* GPUResourceManager::mapResource(ResourceHandle handle)
	{
		Resource* pResource = nullptr;
		decodeAllocationHandle(handle, &pResource, nullptr);

		D3D12_RANGE readRange = { 0, 0 };

		void* pMappedData = nullptr;
		DX_CHECK(pResource->pDXResource->Map(0, &readRange, &pMappedData));

		return pMappedData;
	}

	void GPUResourceManager::unmapResource(ResourceHandle handle)
	{
		Resource* pResource = nullptr;
		decodeAllocationHandle(handle, &pResource, nullptr);

		pResource->pDXResource->Unmap(0, nullptr);
	}

	const ResourceAllocation& GPUResourceManager::getAllocation(AllocationHandle handle)
	{
		ResourceAllocation* pAllocation = nullptr;
		decodeAllocationHandle(handle, nullptr, &pAllocation);

		return *pAllocation;
	}

	uint32_t GPUResourceManager::getTotalSize(AllocationHandle handle)
	{
		ResourceAllocation* pAllocation = nullptr;
		decodeAllocationHandle(handle, nullptr, &pAllocation);

		return pAllocation->elementSize * pAllocation->numElements;
	}

	DescriptorDesc GPUResourceManager::createDescriptorDesc(AllocationHandle handle, DescriptorType type, bool nullDesc)
	{
		// handle asserted in decodeHandle
		OKAY_ASSERT(type != OKAY_DESCRIPTOR_TYPE_NONE);

		Resource* pResource = nullptr;
		ResourceAllocation* pAllocation = nullptr;

		decodeAllocationHandle(handle, &pResource, &pAllocation);

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

	void GPUResourceManager::updateBufferInternal(const Resource& resource, const ResourceAllocation& allocation, const void* pData)
	{
		if (resource.heapType == D3D12_HEAP_TYPE_DEFAULT)
		{
			updateBufferUpload(resource.pDXResource, allocation.resourceOffset, allocation.elementSize * allocation.numElements, pData);
		}
		else
		{
			updateBufferDirect(resource.pDXResource, allocation.resourceOffset, allocation.elementSize * allocation.numElements, pData);
		}
	}

	void GPUResourceManager::updateBufferUpload(ID3D12Resource* pDXResource, uint64_t resourceOffset, uint32_t byteSize, const void* pData)
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

	void GPUResourceManager::updateBufferDirect(ID3D12Resource* pDXResource, uint64_t resourceOffset, uint32_t byteSize, const void* pData)
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

		D3D12_RESOURCE_ALLOCATION_INFO resourceAllocationInfo = m_pDevice->GetResourceAllocationInfo(0, 1, &textureDesc);
		ID3D12Resource* pUploadResource = nullptr;
		createUploadResource(&pUploadResource, resourceAllocationInfo.SizeInBytes);

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

	AllocationHandle GPUResourceManager::generateAllocationHandle(ResourceHandle resourceHandle, uint16_t allocationIndex)
	{
		AllocationHandle handle = 0;
		uint16_t* pHandle = (uint16_t*)&handle;

		pHandle[HANDLE_RESOURCE_IDX_SLOT] = resourceHandle;
		pHandle[HANDLE_ALLOCATION_IDX_SLOT] = allocationIndex;

		return handle;
	}

	void GPUResourceManager::decodeAllocationHandle(AllocationHandle handle, Resource** ppOutResource, ResourceAllocation** ppOutAllocation)
	{
		validateAllocationHandle(handle);

		uint16_t* pHandle = (uint16_t*)&handle;

		uint16_t resourceIndex = pHandle[HANDLE_RESOURCE_IDX_SLOT];
		uint16_t allocationIndex = pHandle[HANDLE_ALLOCATION_IDX_SLOT];

		if (ppOutResource)
			*ppOutResource = &m_resources[resourceIndex];
		
		if (ppOutAllocation)
			*ppOutAllocation = &m_allocations[allocationIndex];
	}

	void GPUResourceManager::validateResourceHandle(ResourceHandle handle)
	{
		OKAY_ASSERT(handle < (uint16_t)m_resources.size());
	}

	void GPUResourceManager::validateAllocationHandle(AllocationHandle handle)
	{
		uint16_t* pHandle = (uint16_t*)&handle;

		uint16_t resourceIndex = pHandle[HANDLE_RESOURCE_IDX_SLOT];
		uint16_t allocationIndex = pHandle[HANDLE_ALLOCATION_IDX_SLOT];

		OKAY_ASSERT(resourceIndex < (uint16_t)m_resources.size());
		OKAY_ASSERT(allocationIndex < (uint16_t)m_allocations.size());
	}

	AllocationHandle GPUResourceManager::addBufferInternal(ResourceHandle handle, uint32_t elementSize, uint32_t elementCount, const void* pData)
	{
		validateResourceHandle(handle);
		Resource& resoruce = m_resources[handle];

		uint32_t gpuAllocationSize = alignAddress32(elementSize * elementCount, BUFFER_DATA_ALIGNMENT);
		OKAY_ASSERT(gpuAllocationSize <= resoruce.maxSize - resoruce.usedSize);

		uint16_t allocationIdx = (uint16_t)m_allocations.size();

		ResourceAllocation& allocation = m_allocations.emplace_back();
		allocation.resourceOffset = resoruce.usedSize;
		allocation.numElements = elementCount;
		allocation.elementSize = elementSize;

		if (pData)
		{
			updateBufferInternal(resoruce, allocation, pData);
		}

		resoruce.usedSize += gpuAllocationSize;

		return generateAllocationHandle(handle, allocationIdx);
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
