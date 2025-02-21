#pragma once

#include "Engine/Okay.h"
#include "CommandContext.h"

#include <d3d12.h>

#define RESOURCE_PLACEMENT_ALIGNMENT	D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
#define BUFFER_DATA_ALIGNMENT			D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT
#define TEXTURE_DATA_ALIGNMENT			D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT

namespace Okay
{
	typedef uint32_t ResourceHandle;

	enum class DescriptorType : uint32_t
	{
		NONE = INVALID_UINT32,
		RTV = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
		CBV_SRV_UAV = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		DSV = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
	};

	struct DescriptorStorage
	{
		ID3D12DescriptorHeap* pDescriptorHeap = nullptr;

		uint32_t maxDescriptors = INVALID_UINT32;
		uint32_t numDescriptors = INVALID_UINT32;

		uint32_t descriptorSize = INVALID_UINT32;
	};

	struct GPUTexture
	{
		ID3D12Resource* pResource = nullptr;
		uint32_t descriptorOffset = INVALID_UINT32;
	};

	struct TextureStorage
	{
		std::vector<GPUTexture> gpuTextures;
		ID3D12Heap* pHeap = nullptr;

		uint64_t maxHeapSize = INVALID_UINT32;
		uint64_t currentHeapSize = INVALID_UINT32;
	};

	struct GPUBuffer
	{
		uint32_t elementSize = INVALID_UINT32;
		uint32_t numElements = INVALID_UINT32;

		uint32_t resourceOffset = INVALID_UINT32;
		uint32_t descriptorOffset = INVALID_UINT32;
	};

	struct BufferStorage
	{
		std::vector<GPUBuffer> gpuBuffers;
		ID3D12Resource* pResource = nullptr;

		uint32_t maxResourceSize = INVALID_UINT32;
		uint32_t currentResourceSize = INVALID_UINT32;
	};

	enum class TextureFormat : uint32_t
	{
		NONE = INVALID_UINT32,
		U_8X1 = DXGI_FORMAT_R8_UNORM,
		U_8X4 = DXGI_FORMAT_R8G8B8A8_UNORM,
		DEPTH = DXGI_FORMAT_D32_FLOAT,
	};

	enum class TextureFlags : uint32_t
	{
		NONE = 0,
		RENDER = 1,
		SHADER_READ = 2,
		DEPTH = 4,
	};

	enum class BufferUsage : uint32_t
	{
		NONE = 0,
		STATIC = 1,
		DYNAMIC = 2,
	};

	enum class ResourceType : uint32_t
	{
		NONE = 0,
		BUFFER = 1,
		TEXTURE = 2,
	};

	class GPUResourceManager
	{
	public:
		GPUResourceManager() = default;
		virtual ~GPUResourceManager();

		void initialize(ID3D12Device* pDevice, CommandContext& commandContext);
		void shutdown();

		ResourceHandle addTexture(uint32_t width, uint32_t height, TextureFormat format, uint32_t flags, void* pData);

		ResourceHandle addConstantBuffer(BufferUsage usage, uint32_t byteSize, void* pData);
		ResourceHandle addStructuredBuffer(BufferUsage usage, uint32_t elementSize, uint32_t elementCount, void* pData);
		ResourceHandle addVertexBuffer(uint32_t vertexSize, uint32_t numVerticies, void* pData);
		ResourceHandle addIndexBuffer(uint32_t numIndicies, void* pData);

		// Only for constant & structured buffers
		void updateBuffer(ResourceHandle handle, void* pData);

	private:
		void initBufferStorage(BufferStorage& storage, BufferUsage usage, uint32_t bufferSize);
		void initTextureStorage(TextureStorage& storage, uint64_t size);
		void initDescriptorStorage(DescriptorStorage& storage, DescriptorType type);

		void shutdownBufferStorage(BufferStorage& storage);
		void shutdownTextureStorage(TextureStorage& storage);
		void shutdownDescriptorStorage(DescriptorStorage& storage);

		void createHeap(ID3D12Heap** ppHeap, BufferUsage usage, uint64_t byteSize);
		void createUploadResource(ID3D12Resource** ppResource, uint64_t byteSize);

		void resizeBufferStorage(BufferStorage& storage, BufferUsage usage, uint32_t newSize);
		void resizeUploadBuffer(uint64_t newSize);
		void resizeTextureStorage(TextureStorage& storage, uint64_t newSize);

	private:
		ResourceHandle addBufferInternal(BufferStorage& storage, BufferUsage usage, uint32_t elementSize, uint32_t elementCount, void* pData);
		
		BufferStorage& getBufferStorage(BufferUsage usage);

		void updateBufferUpload(ID3D12Resource* pResource, uint32_t resourceOffset, uint32_t byteSize, void* pData);
		void updateBufferDirect(ID3D12Resource* pResource, uint32_t resourceOffset, uint32_t byteSize, void* pData);
		void updateTexture(ID3D12Resource* pResource, unsigned char* pData);

	private:
		ID3D12Device* m_pDevice = nullptr;
		CommandContext* m_pCommandContext = nullptr;

		ID3D12Resource* m_pUploadBuffer = nullptr;
		uint64_t m_uploadHeapCurrentSize = INVALID_UINT64;
		uint64_t m_uploadHeapMaxSize = INVALID_UINT64;

		BufferStorage m_staticBuffers;
		BufferStorage m_dynamicBuffers;
		BufferStorage m_meshStorage; // Vertex & Index buffer storage, always static (atleast for now)

		TextureStorage m_textureStorage;

		DescriptorStorage m_rtvStorage;
		DescriptorStorage m_cbvSrvUavStorage;
		DescriptorStorage m_dsvStorage;
	};
}
