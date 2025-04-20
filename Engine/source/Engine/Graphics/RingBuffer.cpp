#include "RingBuffer.h"

namespace Okay
{
	void RingBuffer::initialize(ID3D12Device* pDevice, uint64_t size)
	{
		size = alignAddress64(size, RESOURCE_PLACEMENT_ALIGNMENT);

		createBuffer(pDevice, size);

		m_bufferOffset = 0;
		m_maxSize = size;
	}
	
	void RingBuffer::shutdown()
	{
		D3D12_RELEASE(m_pRingBuffer);
	}
	
	D3D12_GPU_VIRTUAL_ADDRESS RingBuffer::allocate(const void* pData, uint64_t byteWidth)
	{
		D3D12_GPU_VIRTUAL_ADDRESS allocationGpuAddress = getCurrentGPUAddress();

		uint8_t* pMappedData = map();
		memcpy(pMappedData, pData, byteWidth);
		unmap(byteWidth);

		return allocationGpuAddress;
	}

	uint64_t RingBuffer::getOffset() const
	{
		return m_bufferOffset;
	}

	D3D12_GPU_VIRTUAL_ADDRESS RingBuffer::getCurrentGPUAddress() const
	{
		return m_pRingBuffer->GetGPUVirtualAddress() + m_bufferOffset;
	}

	ID3D12Resource* RingBuffer::getDXResource() const
	{
		return m_pRingBuffer;
	}
	
	uint8_t* RingBuffer::map()
	{
		D3D12_RANGE mapRange = { 0, 0 };
		uint8_t* pMappedData = nullptr;
		DX_CHECK(m_pRingBuffer->Map(0, &mapRange, (void**)&pMappedData));

		pMappedData += m_bufferOffset;

		return pMappedData;
	}

	void RingBuffer::unmap(uint64_t bytesWritten)
	{
		m_pRingBuffer->Unmap(0, nullptr);

		m_bufferOffset += alignAddress64(bytesWritten, BUFFER_DATA_ALIGNMENT);
		OKAY_ASSERT(m_bufferOffset <= m_maxSize);
	}

	void RingBuffer::jumpToStart()
	{
		m_bufferOffset = 0;
	}

	void RingBuffer::createBuffer(ID3D12Device* pDevice, uint64_t size)
	{
		D3D12_HEAP_PROPERTIES heapProperties = {};
		heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProperties.CreationNodeMask = 0;
		heapProperties.VisibleNodeMask = 0;

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Alignment = RESOURCE_PLACEMENT_ALIGNMENT;
		
		desc.Width = size;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;

		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;

		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		DX_CHECK(pDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_pRingBuffer)));
	}
}
