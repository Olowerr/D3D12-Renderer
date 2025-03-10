#pragma once

#include "Engine/OkayD3D12.h"
#include "CommandContext.h"
#include "HeapStore.h"

#include <string>

namespace Okay
{
	typedef uint64_t ResourceHandle;
	constexpr ResourceHandle INVALID_RH = INVALID_UINT64;

	struct ResourceAllocation
	{
		uint32_t elementSize = INVALID_UINT32;
		uint32_t numElements = INVALID_UINT32;

		uint64_t resourceOffset = INVALID_UINT64;
	};

	struct Resource
	{
		ID3D12Resource* pDXResource = nullptr;

		uint64_t maxSize = INVALID_UINT64;
		uint64_t usedSize = INVALID_UINT64;
	};

	class GPUResourceManager
	{
	public:
		GPUResourceManager() = default;
		virtual ~GPUResourceManager() = default;

		void initialize(ID3D12Device* pDevice, CommandContext& commandContext);
		void shutdown();

		ResourceHandle addTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t flags, void* pData);

		ResourceHandle addConstantBuffer(BufferUsage usage, uint32_t byteSize, void* pData);
		ResourceHandle addStructuredBuffer(BufferUsage usage, uint32_t elementSize, uint32_t elementCount, void* pData);
		ResourceHandle addVertexBuffer(uint32_t vertexSize, uint32_t numVerticies, void* pData);
		ResourceHandle addIndexBuffer(uint32_t numIndicies, void* pData);

		void updateBuffer(ResourceHandle handle, void* pData);

		ID3D12Resource* getDXResource(ResourceHandle handle);
		D3D12_GPU_VIRTUAL_ADDRESS getVirtualAddress(ResourceHandle handle);

		const ResourceAllocation& getAllocation(ResourceHandle handle);
		uint32_t getTotalSize(ResourceHandle handle);

		DescriptorDesc createDescriptorDesc(ResourceHandle handle, DescriptorType type, bool nullDesc);

	private:
		void createUploadResource(ID3D12Resource** ppResource, uint64_t byteSize);
		void resizeUploadBuffer(uint64_t newSize);

		ResourceHandle addBufferInternal(BufferUsage usage, uint32_t elementSize, uint32_t elementCount, void* pData);
		
		D3D12_HEAP_TYPE getHeapType(BufferUsage usage);
		HeapStore& getHeapStore(BufferUsage usage);
		std::vector<Resource>& getResourceList(BufferUsage usage);

		void updateBufferInternal(const Resource& pResource, const ResourceAllocation& allocation, BufferUsage usage, void* pData);
		void updateBufferUpload(ID3D12Resource* pResource, uint64_t resourceOffset, uint32_t byteSize, void* pData);
		void updateBufferDirect(ID3D12Resource* pResource, uint64_t resourceOffset, uint32_t byteSize, void* pData);
		void updateTexture(ID3D12Resource* pResource, unsigned char* pData);

		ResourceHandle generateHandle(uint16_t resourceIndex, uint16_t allocationIndex, BufferUsage usage);
		void decodeHandle(ResourceHandle handle, Resource** ppOutResource, ResourceAllocation** ppOutAllocation, BufferUsage** ppOutUsage);
		void validateDecodedHandle(uint16_t resourceIndex, uint16_t allocationIndex, BufferUsage usage);

	private:
		ID3D12Device* m_pDevice = nullptr;
		CommandContext* m_pCommandContext = nullptr;

		ID3D12Resource* m_pUploadBuffer = nullptr;
		uint64_t m_uploadHeapCurrentSize = INVALID_UINT64;
		uint64_t m_uploadHeapMaxSize = INVALID_UINT64;

		HeapStore m_staticHeapStore;
		HeapStore m_dynamicHeapStore;

		std::vector<Resource> m_staticResources;
		std::vector<Resource> m_dynamicResources;

		std::vector<ResourceAllocation> m_allocations;
	};
}
