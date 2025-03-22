#pragma once

#include "Engine/OkayD3D12.h"
#include "CommandContext.h"
#include "HeapStore.h"

#include <string>

#define OKAY_RESOURCE_APPEND INVALID_UINT64

namespace Okay
{
	typedef uint16_t ResourceHandle;
	constexpr ResourceHandle INVALID_RH = INVALID_UINT16;

	struct Allocation
	{
		ResourceHandle resourceHandle = INVALID_RH;
		uint64_t resourceOffset = INVALID_UINT64;

		uint64_t elementSize = INVALID_UINT64;
		uint32_t numElements = INVALID_UINT32;
	};

	struct Resource
	{
		ID3D12Resource* pDXResource = nullptr;
		D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE(-1);

		uint64_t maxSize = INVALID_UINT64;
		uint64_t nextAppendOffset = INVALID_UINT64;
	};

	class GPUResourceManager
	{
	public:
		GPUResourceManager() = default;
		virtual ~GPUResourceManager() = default;

		void initialize(ID3D12Device* pDevice, CommandContext& commandContext);
		void shutdown();

		Allocation createTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, uint32_t flags, const void* pData);

		ResourceHandle createResource(D3D12_HEAP_TYPE heapType, uint64_t size);
		Allocation allocateInto(ResourceHandle handle, uint64_t offset, uint64_t elementSize, uint32_t numElements, const void* pData);

		// TODO: Rename to updateAllocation?
		void updateBuffer(const Allocation& allocation, const void* pData);

		ID3D12Resource* getDXResource(ResourceHandle handle);
		D3D12_GPU_VIRTUAL_ADDRESS getVirtualAddress(const Allocation& allocation);

		void* mapResource(ResourceHandle handle);
		void unmapResource(ResourceHandle handle);

		DescriptorDesc createDescriptorDesc(const Allocation& allocation, DescriptorType type, bool nullDesc);

	private:
		void createUploadResource(ID3D12Resource** ppResource, uint64_t byteSize);
		void resizeUploadBuffer(uint64_t newSize);

		void updateBufferUpload(ID3D12Resource* pDXResource, uint64_t resourceOffset, uint64_t byteSize, const void* pData);
		void updateBufferDirect(ID3D12Resource* pDXResource, uint64_t resourceOffset, uint64_t byteSize, const void* pData);
		void updateTexture(ID3D12Resource* pDXResource, unsigned char* pData);

		void validateResourceHandle(ResourceHandle handle);

	private:
		ID3D12Device* m_pDevice = nullptr;
		CommandContext* m_pCommandContext = nullptr;

		ID3D12Resource* m_pUploadBuffer = nullptr;
		uint64_t m_uploadHeapCurrentSize = INVALID_UINT64;
		uint64_t m_uploadHeapMaxSize = INVALID_UINT64;

		HeapStore m_heapStore;

		std::vector<Resource> m_resources;
	};
}
