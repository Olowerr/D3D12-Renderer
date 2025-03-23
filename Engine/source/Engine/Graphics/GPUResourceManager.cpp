#include "GPUResourceManager.h"

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

		m_heapStore.shutdown();

		D3D12_RELEASE(m_pUploadBuffer);

		m_pDevice = nullptr;
	}

	Allocation GPUResourceManager::createTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t flags, const void* pData)
	{
		OKAY_ASSERT(width);
		OKAY_ASSERT(height);
		OKAY_ASSERT(TextureFlags(flags) != OKAY_TEXTURE_FLAG_NONE);

		uint16_t resourceIdx = (uint16_t)m_resources.size();

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

		resource.nextAppendOffset = resourceAllocationInfo.SizeInBytes;
		resource.maxSize = resourceAllocationInfo.SizeInBytes;

		if (pData)
		{
			updateTexture(resource.pDXResource, (unsigned char*)pData);
		}

		Allocation allocation = {};
		allocation.resourceHandle = (ResourceHandle)resourceIdx;
		allocation.resourceOffset = 0;
		allocation.elementSize = resource.maxSize;
		allocation.numElements = 1;

		return allocation;
	}

	ResourceHandle GPUResourceManager::createResource(D3D12_HEAP_TYPE heapType, uint64_t size)
	{
		size = alignAddress64(size, BUFFER_DATA_ALIGNMENT);

		ResourceHandle handle = (ResourceHandle)m_resources.size();

		Resource& resource = m_resources.emplace_back();

		resource.pDXResource = m_heapStore.requestResource(heapType, size, 1, 1, DXGI_FORMAT_UNKNOWN, nullptr, false);
		resource.heapType = heapType;

		resource.nextAppendOffset = 0;
		resource.maxSize = size;

		return handle;
	}

	Allocation GPUResourceManager::allocateInto(ResourceHandle handle, uint64_t offset, uint64_t elementSize, uint32_t numElements, const void* pData)
	{
		validateResourceHandle(handle);
		Resource& resource = m_resources[handle];

		if (offset == 0 && elementSize == 0 && numElements == 1)
		{
			elementSize = resource.maxSize;
		}
		else if (offset == OKAY_RESOURCE_APPEND)
		{
			offset = resource.nextAppendOffset;
			OKAY_ASSERT(offset + elementSize * numElements <= resource.maxSize);
		}

		resource.nextAppendOffset += alignAddress64(elementSize * numElements, BUFFER_DATA_ALIGNMENT);

		Allocation allocation = {};
		allocation.resourceHandle = handle;
		allocation.resourceOffset = alignAddress64(offset, BUFFER_DATA_ALIGNMENT);
		allocation.elementSize = elementSize;
		allocation.numElements = numElements;

		if (pData)
		{
			updateBuffer(allocation, pData);
		}

		return allocation;
	}

	void GPUResourceManager::updateBuffer(const Allocation& allocation, const void* pData)
	{
		validateResourceHandle(allocation.resourceHandle);
		Resource& resource = m_resources[allocation.resourceHandle];

		if (resource.heapType == D3D12_HEAP_TYPE_DEFAULT)
		{
			updateBufferUpload(resource.pDXResource, allocation.resourceOffset, allocation.elementSize * allocation.numElements, pData);
		}
		else
		{
			updateBufferDirect(resource.pDXResource, allocation.resourceOffset, allocation.elementSize * allocation.numElements, pData);
		}
	}

	ID3D12Resource* GPUResourceManager::getDXResource(ResourceHandle handle)
	{
		validateResourceHandle(handle);
		return m_resources[handle].pDXResource;
	}

	D3D12_GPU_VIRTUAL_ADDRESS GPUResourceManager::getVirtualAddress(const Allocation& allocation)
	{
		ID3D12Resource* pDXResource = getDXResource(allocation.resourceHandle);
		return pDXResource->GetGPUVirtualAddress() + allocation.resourceOffset;
	}

	void* GPUResourceManager::mapResource(ResourceHandle handle)
	{
		ID3D12Resource* pDXResource = getDXResource(handle);

		D3D12_RANGE readRange = { 0, 0 };

		void* pMappedData = nullptr;
		DX_CHECK(pDXResource->Map(0, &readRange, &pMappedData));

		return pMappedData;
	}

	void GPUResourceManager::unmapResource(ResourceHandle handle)
	{
		ID3D12Resource* pDXResource = getDXResource(handle);
		pDXResource->Unmap(0, nullptr);
	}

	DescriptorDesc GPUResourceManager::createDescriptorDesc(const Allocation& allocation, DescriptorType type, bool nullDesc)
	{
		// handle asserted in decodeHandle
		OKAY_ASSERT(type != OKAY_DESCRIPTOR_TYPE_NONE);

		ID3D12Resource* pDXResource = getDXResource(allocation.resourceHandle);

		DescriptorDesc desc = {};
		desc.type = type;
		desc.nullDesc = nullDesc;
		desc.pDXResource = pDXResource;

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
			desc.cbvDesc.BufferLocation = pDXResource->GetGPUVirtualAddress() + allocation.resourceOffset;
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

	void GPUResourceManager::updateBufferUpload(ID3D12Resource* pDXResource, uint64_t resourceOffset, uint64_t byteSize, const void* pData)
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

	void GPUResourceManager::updateBufferDirect(ID3D12Resource* pDXResource, uint64_t resourceOffset, uint64_t byteSize, const void* pData)
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

	void GPUResourceManager::validateResourceHandle(ResourceHandle handle)
	{
		OKAY_ASSERT(handle < (ResourceHandle)m_resources.size());
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
