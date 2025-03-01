#pragma once

#include "Engine/Okay.h"
#include "CommandContext.h"
#include "HeapStore.h"

#include <d3d12.h>

namespace Okay
{
	typedef uint64_t ResourceHandle;

	// Describes the byte offsets used for the different parts of a ResourceHandle
	// Currently assuming each part 'only' needs 16 bits
	constexpr uint8_t HANDLE_RESOURCE_IDX_OFFSET = 0;
	constexpr uint8_t HANDLE_ALLOCATION_IDX_OFFSET = 2;
	constexpr uint8_t HANDLE_USAGE_OFFSET = 4;

	struct ResourceAllocation
	{
		uint32_t elementSize = INVALID_UINT32;
		uint32_t numElements = INVALID_UINT32;
		
		uint64_t resourceOffset = INVALID_UINT64;
	};

	struct Resource
	{
		ID3D12Resource* pDXResource = nullptr;
		uint64_t usedSize = INVALID_UINT64;
	};

	enum TextureFlags : uint32_t
	{
		OKAY_TEXTURE_FLAG_NONE = 0,
		OKAY_TEXTURE_FLAG_RENDER = 1,
		OKAY_TEXTURE_FLAG_SHADER_READ = 2,
		OKAY_TEXTURE_FLAG_DEPTH = 4,
	};

	enum BufferUsage : uint8_t
	{
		OKAY_BUFFER_USAGE_NONE = 0,
		OKAY_BUFFER_USAGE_STATIC = 1,
		OKAY_BUFFER_USAGE_DYNAMIC = 2,
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
		void decodeHandle(ResourceHandle handle, Resource** ppOutResource, ResourceAllocation** ppOutAllocation, BufferUsage** ppUsage);

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
