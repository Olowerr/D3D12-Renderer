#include "GPUResourceManager.h"



namespace Okay
{
	GPUResourceManager::~GPUResourceManager()
	{
		shutdown();
	}

	void GPUResourceManager::initialize(ID3D12Device* pDevice)
	{
		m_pDevice = pDevice;

		initBufferStorage(m_staticBuffers, BufferUsage::STATIC);
		initBufferStorage(m_dynamicStorage, BufferUsage::DYNAMIC);
		initBufferStorage(m_meshStorage, BufferUsage::STATIC);

		initTextureStorage(m_textureStorage);

		initDescriptorStorage(m_rtvStorage, DescriptorType::RTV);
		initDescriptorStorage(m_cbvSrvUavStorage, DescriptorType::CBV_SRV_UAV);
		initDescriptorStorage(m_dsvStorage, DescriptorType::DSV);
	}

	void GPUResourceManager::shutdown()
	{
		shutdownBufferStorage(m_staticBuffers);
		shutdownBufferStorage(m_dynamicStorage);
		shutdownBufferStorage(m_meshStorage);

		shutdownTextureStorage(m_textureStorage);

		shutdownDescriptorStorage(m_rtvStorage);
		shutdownDescriptorStorage(m_cbvSrvUavStorage);
		shutdownDescriptorStorage(m_dsvStorage);

		m_pDevice = nullptr;
	}

	ResourceHandle GPUResourceManager::addTexture(uint32_t width, uint32_t height, TextureFormat format, TextureFlags flags)
	{
		ResourceHandle handle = INVALID_UINT;
		return handle;
	}

	ResourceHandle GPUResourceManager::addConstantBuffer(BufferUsage usage, uint32_t byteSize, void* pData)
	{
		ResourceHandle handle = INVALID_UINT;
		return handle;
	}

	ResourceHandle GPUResourceManager::addStructuredBuffer(BufferUsage usage, uint32_t elementSize, uint32_t elementCount, void* pData)
	{
		ResourceHandle handle = INVALID_UINT;
		return handle;
	}

	ResourceHandle GPUResourceManager::addVertexBuffer(uint32_t vertexSize, uint32_t numVerticies, void* pData)
	{
		ResourceHandle handle = INVALID_UINT;
		return handle;
	}

	ResourceHandle GPUResourceManager::addIndexBuffer(uint32_t numIndicies, void* pData)
	{
		ResourceHandle handle = INVALID_UINT;
		return handle;
	}

	void GPUResourceManager::initBufferStorage(BufferStorage& storage, BufferUsage usage)
	{
		shutdownBufferStorage(storage);

		createHeap(&storage.pHeap, usage);

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Alignment = BUFFER_PLACEMENT_ALIGNMENT;
		resourceDesc.Width = BUFFER_PLACEMENT_ALIGNMENT;
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		D3D12_RESOURCE_STATES initialState = usage == BufferUsage::STATIC ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_GENERIC_READ;

		DX_CHECK(m_pDevice->CreatePlacedResource(storage.pHeap, 0, &resourceDesc, initialState, nullptr, IID_PPV_ARGS(&storage.pResource)));

		storage.currentHeapSize = 0;
		storage.maxHeapSize = BUFFER_PLACEMENT_ALIGNMENT;
	}

	void GPUResourceManager::initTextureStorage(TextureStorage& storage)
	{
		shutdownTextureStorage(storage);

		createHeap(&storage.pHeap, BufferUsage::STATIC);

		storage.currentHeapSize = 0;
		storage.maxHeapSize = BUFFER_PLACEMENT_ALIGNMENT;
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
		D3D12_RELEASE(storage.pHeap);

		storage.currentHeapSize = INVALID_UINT;
		storage.maxHeapSize = INVALID_UINT;

		storage.gpuBuffers.clear();
	}

	void GPUResourceManager::shutdownTextureStorage(TextureStorage& storage)
	{
		for (GPUTexture gpuTexture : storage.gpuTextures)
			D3D12_RELEASE(gpuTexture.pResource);

		D3D12_RELEASE(storage.pHeap);

		storage.currentHeapSize = INVALID_UINT;
		storage.maxHeapSize = INVALID_UINT;

		storage.gpuTextures.clear();
	}

	void GPUResourceManager::shutdownDescriptorStorage(DescriptorStorage& storage)
	{
		D3D12_RELEASE(storage.pDescriptorHeap);
		storage = DescriptorStorage();
	}

	void GPUResourceManager::createHeap(ID3D12Heap** ppHeap, BufferUsage usage, uint32_t byteSize)
	{
		D3D12_HEAP_DESC heapDesc{};
		heapDesc.SizeInBytes = byteSize;

		heapDesc.Properties.Type = usage == BufferUsage::STATIC ? D3D12_HEAP_TYPE_DEFAULT : D3D12_HEAP_TYPE_UPLOAD;
		heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapDesc.Properties.CreationNodeMask = 0;
		heapDesc.Properties.VisibleNodeMask = 0;

		heapDesc.Alignment = BUFFER_PLACEMENT_ALIGNMENT;
		heapDesc.Flags = D3D12_HEAP_FLAG_NONE;

		DX_CHECK(m_pDevice->CreateHeap(&heapDesc, IID_PPV_ARGS(ppHeap)));
	}
}
