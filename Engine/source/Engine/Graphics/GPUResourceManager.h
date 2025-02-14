#pragma once

#include "Engine/Okay.h"

#include <d3d12.h>

#define BUFFER_PLACEMENT_ALIGNMENT	D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
#define BUFFER_DATA_ALIGNMENT		D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT

namespace Okay
{
	typedef uint32_t ResourceHandle;

	enum class DescriptorType : uint32_t
	{
		NONE = INVALID_UINT,
		RTV = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
		CBV_SRV_UAV = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		DSV = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
	};

	struct DescriptorStorage
	{
		ID3D12DescriptorHeap* pDescriptorHeap = nullptr;

		uint32_t maxDescriptors = INVALID_UINT;
		uint32_t numDescriptors = INVALID_UINT;

		uint32_t descriptorSize = INVALID_UINT;
	};

	struct GPUTexture
	{
		ID3D12Resource* pResource = nullptr;
		uint32_t descriptorOffset = INVALID_UINT;
	};

	struct TextureStorage
	{
		std::vector<GPUTexture> gpuTextures;
		ID3D12Heap* pHeap = nullptr;

		uint32_t maxHeapSize = INVALID_UINT;
		uint32_t currentHeapSize = INVALID_UINT;
	};

	struct GPUBuffer
	{
		uint32_t elementSize = INVALID_UINT;
		uint32_t numElements = INVALID_UINT;

		uint32_t resourceOffset = INVALID_UINT;
		uint32_t descriptorOffset = INVALID_UINT;
	};

	struct BufferStorage
	{
		std::vector<GPUBuffer> gpuBuffers;
		ID3D12Resource* pResource = nullptr;
		ID3D12Heap* pHeap = nullptr;

		uint32_t maxHeapSize = INVALID_UINT;
		uint32_t currentHeapSize = INVALID_UINT;
	};

	enum class TextureFormat : uint32_t
	{
		NONE = INVALID_UINT,
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

	enum class BufferUsage
	{
		NONE = 0,
		STATIC = 1,
		DYNAMIC = 2,
	};

	class GPUResourceManager
	{
	public:
		GPUResourceManager() = default;
		virtual ~GPUResourceManager();

		void initialize(ID3D12Device* pDevice);
		void shutdown();

		ResourceHandle addTexture(uint32_t width, uint32_t height, TextureFormat format, TextureFlags flags);

		ResourceHandle addConstantBuffer(BufferUsage usage, uint32_t byteSize, void* pData);
		ResourceHandle addStructuredBuffer(BufferUsage usage, uint32_t elementSize, uint32_t elementCount, void* pData);
		ResourceHandle addVertexBuffer(uint32_t vertexSize, uint32_t numVerticies, void* pData);
		ResourceHandle addIndexBuffer(uint32_t numIndicies, void* pData);

	private:
		void initBufferStorage(BufferStorage& storage, BufferUsage usage);
		void initTextureStorage(TextureStorage& storage);
		void initDescriptorStorage(DescriptorStorage& storage, DescriptorType type);

		void shutdownBufferStorage(BufferStorage& storage);
		void shutdownTextureStorage(TextureStorage& storage);
		void shutdownDescriptorStorage(DescriptorStorage& storage);

	private:
		void createHeap(ID3D12Heap** ppHeap, BufferUsage usage, uint32_t byteSize = BUFFER_PLACEMENT_ALIGNMENT);

	private:
		ID3D12Device* m_pDevice = nullptr;

		BufferStorage m_staticBuffers;
		BufferStorage m_dynamicStorage;
		BufferStorage m_meshStorage; // Vertex & Index buffer storage, always static (atleast for now)

		TextureStorage m_textureStorage;

		DescriptorStorage m_rtvStorage;
		DescriptorStorage m_cbvSrvUavStorage;
		DescriptorStorage m_dsvStorage;
	};
}
