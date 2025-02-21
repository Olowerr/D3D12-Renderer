#include "GPUResourceManager.h"

namespace Okay
{
	// TODO: Try not flushing when resizing resources


	GPUResourceManager::~GPUResourceManager()
	{
		shutdown();
	}

	void GPUResourceManager::initialize(ID3D12Device* pDevice, CommandContext& commandContext)
	{
		m_pDevice = pDevice;
		m_pCommandContext = &commandContext;

		initBufferStorage(m_staticBuffers, BufferUsage::STATIC, RESOURCE_PLACEMENT_ALIGNMENT);
		initBufferStorage(m_dynamicBuffers, BufferUsage::DYNAMIC, RESOURCE_PLACEMENT_ALIGNMENT);
		initBufferStorage(m_meshStorage, BufferUsage::STATIC, RESOURCE_PLACEMENT_ALIGNMENT);

		initTextureStorage(m_textureStorage, (uint64_t)2048 * 2048 * 4 * 4); // Start with space for 4 2084*2048 textures

		initDescriptorStorage(m_rtvStorage, DescriptorType::RTV);
		initDescriptorStorage(m_cbvSrvUavStorage, DescriptorType::CBV_SRV_UAV);
		initDescriptorStorage(m_dsvStorage, DescriptorType::DSV);

		createUploadResource(&m_pUploadBuffer, RESOURCE_PLACEMENT_ALIGNMENT);
		m_uploadHeapCurrentSize = 0;
		m_uploadHeapMaxSize = RESOURCE_PLACEMENT_ALIGNMENT;

		m_pCommandContext->flush();
	}

	void GPUResourceManager::shutdown()
	{
		shutdownBufferStorage(m_staticBuffers);
		shutdownBufferStorage(m_dynamicBuffers);
		shutdownBufferStorage(m_meshStorage);

		shutdownTextureStorage(m_textureStorage);

		shutdownDescriptorStorage(m_rtvStorage);
		shutdownDescriptorStorage(m_cbvSrvUavStorage);
		shutdownDescriptorStorage(m_dsvStorage);

		D3D12_RELEASE(m_pUploadBuffer);

		m_pDevice = nullptr;
	}

	ResourceHandle GPUResourceManager::addTexture(uint32_t width, uint32_t height, TextureFormat format, uint32_t flags, void* pData)
	{
		OKAY_ASSERT(width);
		OKAY_ASSERT(height);
		OKAY_ASSERT(format != TextureFormat::NONE);
		OKAY_ASSERT(TextureFlags(flags) != TextureFlags::NONE); // Yikes?
		OKAY_ASSERT(pData);

		D3D12_RESOURCE_DESC textureDesc{};
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		textureDesc.Alignment = RESOURCE_PLACEMENT_ALIGNMENT;
		textureDesc.Width = width;
		textureDesc.Height = height;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.MipLevels = 1; // 1 For now atleast
		textureDesc.Format = DXGI_FORMAT(format); // Yikes?
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		textureDesc.Flags = CHECK_BIT(flags, (uint32_t)TextureFlags::DEPTH) ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_NONE;

		uint64_t textureTotalSize = 0;
		m_pDevice->GetCopyableFootprints(&textureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &textureTotalSize);

		if (m_textureStorage.currentHeapSize + textureTotalSize > m_textureStorage.maxHeapSize)
		{
			resizeTextureStorage(m_textureStorage, uint64_t((m_textureStorage.currentHeapSize + textureTotalSize) * 1.2f));
		}

		GPUTexture& gpuTexture = m_textureStorage.gpuTextures.emplace_back();

		DX_CHECK(m_pDevice->CreatePlacedResource(m_textureStorage.pHeap, m_textureStorage.currentHeapSize, &textureDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&gpuTexture.pResource)));

		updateTexture(gpuTexture.pResource, (unsigned char*)pData);

		m_textureStorage.currentHeapSize = Okay::alignAddress64(m_textureStorage.currentHeapSize + textureTotalSize, TEXTURE_DATA_ALIGNMENT);

		return ResourceHandle(m_textureStorage.gpuTextures.size() - 1);
	}

	ResourceHandle GPUResourceManager::addConstantBuffer(BufferUsage usage, uint32_t byteSize, void* pData)
	{
		OKAY_ASSERT(usage != BufferUsage::NONE);
		OKAY_ASSERT(byteSize);

		BufferStorage& storage = getBufferStorage(usage);
		return addBufferInternal(storage, usage, byteSize, 1, pData);
	}

	ResourceHandle GPUResourceManager::addStructuredBuffer(BufferUsage usage, uint32_t elementSize, uint32_t elementCount, void* pData)
	{
		OKAY_ASSERT(usage != BufferUsage::NONE);
		OKAY_ASSERT(elementSize);
		OKAY_ASSERT(elementCount);

		BufferStorage& storage = getBufferStorage(usage);
		return addBufferInternal(storage, usage, elementSize, elementCount, pData);
	}

	ResourceHandle GPUResourceManager::addVertexBuffer(uint32_t vertexSize, uint32_t numVerticies, void* pData)
	{
		OKAY_ASSERT(vertexSize);
		OKAY_ASSERT(numVerticies);
		OKAY_ASSERT(pData);

		return addBufferInternal(m_meshStorage, BufferUsage::STATIC, vertexSize, numVerticies, pData);
	}

	ResourceHandle GPUResourceManager::addIndexBuffer(uint32_t numIndicies, void* pData)
	{
		OKAY_ASSERT(numIndicies);
		OKAY_ASSERT(pData);

		return addBufferInternal(m_meshStorage, BufferUsage::STATIC, sizeof(uint32_t), numIndicies, pData);
	}

	void GPUResourceManager::updateBuffer(ResourceHandle handle, void* pData)
	{
		OKAY_ASSERT(handle < m_dynamicBuffers.gpuBuffers.size());
		OKAY_ASSERT(pData);

		const GPUBuffer& gpuBuffer = m_dynamicBuffers.gpuBuffers[handle];
		updateBufferDirect(m_dynamicBuffers.pResource, gpuBuffer.resourceOffset, gpuBuffer.elementSize * gpuBuffer.numElements, pData);
	}

	void GPUResourceManager::updateBufferUpload(ID3D12Resource* pResource, uint32_t resourceOffset, uint32_t byteSize, void* pData)
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
		pCommandList->CopyBufferRegion(pResource, resourceOffset, m_pUploadBuffer, m_uploadHeapCurrentSize, byteSize);
		

		m_uploadHeapCurrentSize = alignAddress64(m_uploadHeapCurrentSize + (uint64_t)byteSize, BUFFER_DATA_ALIGNMENT);
	}

	void GPUResourceManager::updateBufferDirect(ID3D12Resource* pResource, uint32_t resourceOffset, uint32_t byteSize, void* pData)
	{
		D3D12_RANGE readRange = { 0, 0 };

		unsigned char* pMappedData = nullptr;
		DX_CHECK(pResource->Map(0, &readRange, (void**)&pMappedData));

		memcpy(pMappedData + resourceOffset, pData, byteSize);

		pResource->Unmap(0, nullptr);
	}

	void GPUResourceManager::updateTexture(ID3D12Resource* pResource, unsigned char* pData)
	{
		/*
			Currently works by:
				1. Create new upload heap
				2. CPU copy texture data into upload heap
				3. Copy upload heap to target resource

			Can create the upload heap at initialization, but it might not be used a lot during "gameplay"
			and might need to resize

			Maybe can also try copying from CPU directly into target?
			Since now we need to create a new upload heap everytime, so maybe worth ?
		*/

		D3D12_RESOURCE_DESC textureDesc = pResource->GetDesc();

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
		copyDst.pResource = pResource;
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

	ResourceHandle GPUResourceManager::addBufferInternal(BufferStorage& storage, BufferUsage usage, uint32_t elementSize, uint32_t elementCount, void* pData)
	{
		uint32_t totalGPUByteSize = alignAddress32(elementSize * elementCount, BUFFER_DATA_ALIGNMENT);

		if (storage.currentResourceSize + totalGPUByteSize > storage.maxResourceSize)
		{
			resizeBufferStorage(storage, usage, uint32_t((storage.currentResourceSize + totalGPUByteSize) * 1.2f));
		}

		GPUBuffer& buffer = storage.gpuBuffers.emplace_back();
		buffer.resourceOffset = storage.currentResourceSize;
		buffer.numElements = elementCount;
		buffer.elementSize = elementSize;
		buffer.descriptorOffset = 0; // TODO: Implement descriptor creation

		storage.currentResourceSize += totalGPUByteSize;

		if (pData)
		{
			if (usage == BufferUsage::STATIC)
			{
				updateBufferUpload(storage.pResource, buffer.resourceOffset, totalGPUByteSize, pData);
			}
			else
			{
				updateBufferDirect(storage.pResource, buffer.resourceOffset, totalGPUByteSize, pData);
			}
		}

		return ResourceHandle(storage.gpuBuffers.size() - 1);
	}

	BufferStorage& GPUResourceManager::getBufferStorage(BufferUsage usage)
	{
		return usage == BufferUsage::STATIC ? m_staticBuffers : m_dynamicBuffers;
	}

	void GPUResourceManager::initBufferStorage(BufferStorage& storage, BufferUsage usage, uint32_t bufferSize)
	{
		shutdownBufferStorage(storage);

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Alignment = RESOURCE_PLACEMENT_ALIGNMENT;
		resourceDesc.Width = bufferSize;
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = usage == BufferUsage::STATIC ? D3D12_HEAP_TYPE_DEFAULT : D3D12_HEAP_TYPE_UPLOAD;
		heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProperties.CreationNodeMask = 0;
		heapProperties.VisibleNodeMask = 0;

		D3D12_RESOURCE_STATES initialState = usage == BufferUsage::STATIC ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_GENERIC_READ;

		DX_CHECK(m_pDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, initialState, nullptr, IID_PPV_ARGS(&storage.pResource)));

		storage.currentResourceSize = 0;
		storage.maxResourceSize = (uint32_t)resourceDesc.Width;
	}

	void GPUResourceManager::initTextureStorage(TextureStorage& storage, uint64_t size)
	{
		shutdownTextureStorage(storage);

		createHeap(&storage.pHeap, BufferUsage::STATIC, size);

		storage.currentHeapSize = 0;
		storage.maxHeapSize = size;
	}

	void GPUResourceManager::initDescriptorStorage(DescriptorStorage& storage, DescriptorType type)
	{
		shutdownDescriptorStorage(storage);

		D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
		descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE(type); // Yikes?
		descriptorHeapDesc.NumDescriptors = 50;
		descriptorHeapDesc.NodeMask = 0;
		descriptorHeapDesc.Flags = type == DescriptorType::CBV_SRV_UAV ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		DX_CHECK(m_pDevice->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&storage.pDescriptorHeap)));

		storage.numDescriptors = 0;
		storage.maxDescriptors = 50;
		storage.descriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(descriptorHeapDesc.Type);
	}

	void GPUResourceManager::shutdownBufferStorage(BufferStorage& storage)
	{
		D3D12_RELEASE(storage.pResource);

		storage.currentResourceSize = INVALID_UINT32;
		storage.maxResourceSize = INVALID_UINT32;

		storage.gpuBuffers.clear();
	}

	void GPUResourceManager::shutdownTextureStorage(TextureStorage& storage)
	{
		for (GPUTexture gpuTexture : storage.gpuTextures)
		{
			D3D12_RELEASE(gpuTexture.pResource);
		}

		D3D12_RELEASE(storage.pHeap);

		storage.currentHeapSize = INVALID_UINT32;
		storage.maxHeapSize = INVALID_UINT32;

		storage.gpuTextures.clear();
	}

	void GPUResourceManager::shutdownDescriptorStorage(DescriptorStorage& storage)
	{
		D3D12_RELEASE(storage.pDescriptorHeap);
		storage = DescriptorStorage();
	}

	void GPUResourceManager::createHeap(ID3D12Heap** ppHeap, BufferUsage usage, uint64_t byteSize)
	{
		D3D12_HEAP_DESC heapDesc{};
		heapDesc.SizeInBytes = byteSize;

		heapDesc.Properties.Type = usage == BufferUsage::STATIC ? D3D12_HEAP_TYPE_DEFAULT : D3D12_HEAP_TYPE_UPLOAD;
		heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapDesc.Properties.CreationNodeMask = 0;
		heapDesc.Properties.VisibleNodeMask = 0;

		heapDesc.Alignment = RESOURCE_PLACEMENT_ALIGNMENT;
		heapDesc.Flags = D3D12_HEAP_FLAG_NONE;

		DX_CHECK(m_pDevice->CreateHeap(&heapDesc, IID_PPV_ARGS(ppHeap)));
	}

	void GPUResourceManager::createUploadResource(ID3D12Resource** ppResource, uint64_t byteSize)
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

		DX_CHECK(m_pDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(ppResource)));
	}

	void GPUResourceManager::resizeBufferStorage(BufferStorage& storage, BufferUsage usage, uint32_t newSize)
	{
		BufferStorage tempStorage;
		initBufferStorage(tempStorage, usage, newSize);

		ID3D12GraphicsCommandList* pCommandList = m_pCommandContext->getCommandList();
		pCommandList->CopyBufferRegion(tempStorage.pResource, 0, storage.pResource, 0, storage.currentResourceSize);

		m_pCommandContext->flush();

		D3D12_RELEASE(storage.pResource);

		storage.pResource = tempStorage.pResource;

		// TODO: Handle descriptor problems
	}

	void GPUResourceManager::resizeUploadBuffer(uint64_t newSize)
	{
		m_pCommandContext->flush();
		
		D3D12_RELEASE(m_pUploadBuffer);

		createUploadResource(&m_pUploadBuffer, newSize);
		m_uploadHeapCurrentSize = 0;
		m_uploadHeapMaxSize = newSize;
	}

	void GPUResourceManager::resizeTextureStorage(TextureStorage& storage, uint64_t newSize)
	{
		TextureStorage newStorage;
		initTextureStorage(newStorage, newSize);

		newStorage.gpuTextures.resize(storage.gpuTextures.size());

		ID3D12GraphicsCommandList* pCommandList = m_pCommandContext->getCommandList();

		// Create new ID3D12Resources in the newStorage, and copy over data & descriptors

		for (uint32_t i = 0; i < (uint32_t)storage.gpuTextures.size(); i++)
		{
			const GPUTexture& origGPUTexture = storage.gpuTextures[i];
			GPUTexture& newGPUTexture = newStorage.gpuTextures[i];

			D3D12_RESOURCE_DESC desc = origGPUTexture.pResource->GetDesc();
			DX_CHECK(m_pDevice->CreatePlacedResource(newStorage.pHeap, newStorage.currentHeapSize, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&newGPUTexture.pResource)));

			m_pCommandContext->transitionResource(origGPUTexture.pResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
			pCommandList->CopyResource(newGPUTexture.pResource, origGPUTexture.pResource);

			// TODO: Handle descriptor copying

			uint64_t textureSize = 0;
			m_pDevice->GetCopyableFootprints(&desc, 0, 1, 0, nullptr, nullptr, nullptr, &textureSize);

			newStorage.currentHeapSize = Okay::alignAddress64(newStorage.currentHeapSize + textureSize, TEXTURE_DATA_ALIGNMENT);
		}

		m_pCommandContext->flush();

		shutdownTextureStorage(storage);
		storage = newStorage;
	}
}
