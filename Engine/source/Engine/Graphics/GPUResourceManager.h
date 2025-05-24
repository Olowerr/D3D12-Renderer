#pragma once

#include "Engine/OkayD3D12.h"
#include "CommandContext.h"
#include "HeapStore.h"
#include "RingBuffer.h"
#include "DescriptorHeapStore.h"

#include <string>

#define OKAY_RESOURCE_APPEND INVALID_UINT64

namespace Okay
{
	class GPUResourceManager
	{
		/*
			Idea for future regarding RingBuffer & uploading meshes:

			If we gotta import a bunch of large meshes and their total size would exceed the RingBuffer set in initialize(),
			then maybe a solution would be to have a 'setRingBuffer' function in GPUResourceManager.

			That way the renderercan create a new one with the appropriate size and we can shut it down
			when we know that we won't be importing new meshes for a while (like after initialization).
			It also means that the "renderer ringbuffer" can't be full of meshes that are being uploaded.
		*/

	public:
		GPUResourceManager() = default;
		virtual ~GPUResourceManager() = default;

		void initialize(ID3D12Device* pDevice, CommandContext& commandContext, RingBuffer& ringBuffer, DescriptorHeapStore& descriptorHeapStore);
		void shutdown();

		Allocation createTexture(uint32_t width, uint32_t height, uint16_t mipLevels, uint32_t arraySize, DXGI_FORMAT format, uint32_t flags, const void* pData);

		Resource createResource(D3D12_HEAP_TYPE heapType, uint64_t size);
		Allocation allocateInto(Resource resource, uint64_t offset, uint64_t elementSize, uint32_t numElements, const void* pData);

		// TODO: Rename to updateAllocation?
		void updateBuffer(const Allocation& allocation, const void* pData);

		D3D12_GPU_VIRTUAL_ADDRESS getVirtualAddress(const Allocation& allocation);

		DescriptorDesc createDescriptorDesc(const Allocation& allocation, DescriptorType type, bool nullDesc);

		void generateMipMaps();

	private:
		void updateBufferUpload(ID3D12Resource* pDXResource, uint64_t resourceOffset, uint64_t byteSize, const void* pData);
		void updateBufferDirect(ID3D12Resource* pDXResource, uint64_t resourceOffset, uint64_t byteSize, const void* pData);
		void updateTexture(ID3D12Resource* pDXResource, uint8_t* pData);

		void validateResourceHandle(ResourceHandle handle);

		ID3D12RootSignature* createMipMapRootSignature();
		ID3D12PipelineState* createMipMapPSO(ID3D12RootSignature* pRootSignature);

	private:
		ID3D12Device* m_pDevice = nullptr;

		CommandContext* m_pCommandContext = nullptr;
		RingBuffer* m_pRingBuffer = nullptr;
		DescriptorHeapStore* m_pDescriptorHeapStore = nullptr;

		HeapStore m_heapStore;

		std::vector<Resource> m_resources;

	};
}
