#pragma once

#include "Engine/OkayD3D12.h"
#include "CommandContext.h"
#include "HeapStore.h"

#include <string>

namespace Okay
{
	typedef uint32_t AllocationHandle;
	typedef uint16_t ResourceHandle;

	constexpr AllocationHandle INVALID_AH = INVALID_UINT32;
	constexpr ResourceHandle INVALID_RH = INVALID_UINT16;

	struct ResourceAllocation
	{
		uint32_t elementSize = INVALID_UINT32;
		uint32_t numElements = INVALID_UINT32;

		uint64_t resourceOffset = INVALID_UINT64;
	};

	struct Resource
	{
		ID3D12Resource* pDXResource = nullptr;
		D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE(-1);

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

		AllocationHandle createTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t flags, const void* pData);

		ResourceHandle createResource(D3D12_HEAP_TYPE heapType, uint64_t size);
		AllocationHandle addConstantBuffer(ResourceHandle resourceHandle, uint32_t byteSize, const void* pData);
		AllocationHandle addStructuredBuffer(ResourceHandle resourceHandle,uint32_t elementSize, uint32_t elementCount, const void* pData);

		void updateBuffer(AllocationHandle handle, const void* pData);

		ID3D12Resource* getDXResource(AllocationHandle handle);
		D3D12_GPU_VIRTUAL_ADDRESS getVirtualAddress(AllocationHandle handle);

		void* mapResource(ResourceHandle handle);
		void unmapResource(ResourceHandle handle);

		const ResourceAllocation& getAllocation(AllocationHandle handle);
		uint32_t getTotalSize(AllocationHandle handle);

		DescriptorDesc createDescriptorDesc(AllocationHandle handle, DescriptorType type, bool nullDesc);

	private:
		void createUploadResource(ID3D12Resource** ppResource, uint64_t byteSize);
		void resizeUploadBuffer(uint64_t newSize);

		AllocationHandle addBufferInternal(ResourceHandle handle, uint32_t elementSize, uint32_t elementCount, const void* pData);
	
		void updateBufferInternal(const Resource& resource, const ResourceAllocation& allocation, const void* pData);
		void updateBufferUpload(ID3D12Resource* pDXResource, uint64_t resourceOffset, uint32_t byteSize, const void* pData);
		void updateBufferDirect(ID3D12Resource* pDXResource, uint64_t resourceOffset, uint32_t byteSize, const void* pData);
		void updateTexture(ID3D12Resource* pDXResource, unsigned char* pData);

		AllocationHandle generateAllocationHandle(ResourceHandle resourceHandle, uint16_t allocationIndex);
		void decodeAllocationHandle(AllocationHandle handle, Resource** ppOutResource, ResourceAllocation** ppOutAllocation);

		void validateResourceHandle(ResourceHandle handle);
		void validateAllocationHandle(AllocationHandle handle);

	private:
		ID3D12Device* m_pDevice = nullptr;
		CommandContext* m_pCommandContext = nullptr;

		ID3D12Resource* m_pUploadBuffer = nullptr;
		uint64_t m_uploadHeapCurrentSize = INVALID_UINT64;
		uint64_t m_uploadHeapMaxSize = INVALID_UINT64;

		HeapStore m_heapStore;

		std::vector<Resource> m_resources;
		std::vector<ResourceAllocation> m_allocations;
	};
}
