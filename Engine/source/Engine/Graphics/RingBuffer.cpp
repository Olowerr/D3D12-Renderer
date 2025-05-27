#include "RingBuffer.h"

namespace Okay
{
	void RingBuffer::initialize(ID3D12Device* pDevice, uint64_t size)
	{
		m_pDevice = pDevice;
		resize(size);
	}
	
	void RingBuffer::shutdown()
	{
		D3D12_RELEASE(m_pRingBuffer);
	}

	D3D12_GPU_VIRTUAL_ADDRESS RingBuffer::allocateMapped(const void* pData, uint64_t byteWidth)
	{
		OKAY_ASSERT(m_bufferOffset + byteWidth <= m_maxSize);

		D3D12_GPU_VIRTUAL_ADDRESS allocationGpuAddress = getCurrentGPUAddress();

		memcpy(getMappedPtr(), pData, byteWidth);
		m_bufferOffset += alignAddress64(byteWidth, BUFFER_DATA_ALIGNMENT);

		return allocationGpuAddress;
	}

	uint8_t* RingBuffer::getMappedPtr()
	{
		return m_pMappedPtr + m_bufferOffset;
	}

	void RingBuffer::offsetMappedPtr(uint64_t offset)
	{
		OKAY_ASSERT(m_bufferOffset + offset <= m_maxSize);
		m_bufferOffset += offset;
	}

	uint64_t RingBuffer::getOffset() const
	{
		return m_bufferOffset;
	}

	void RingBuffer::alignOffset(uint32_t alignment)
	{
		m_bufferOffset = alignAddress64(m_bufferOffset, alignment);
		OKAY_ASSERT(m_bufferOffset <= m_maxSize);
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
		OKAY_ASSERT(!m_pMappedPtr);

		D3D12_RANGE mapRange = { 0, 0 };
		DX_CHECK(m_pRingBuffer->Map(0, &mapRange, (void**)&m_pMappedPtr));

		return m_pMappedPtr;
	}

	void RingBuffer::unmap()
	{
		D3D12_RANGE writeRange = { 0, 0 };
		m_pRingBuffer->Unmap(0, &writeRange);

		m_pMappedPtr = nullptr;
		m_bufferOffset = 0;
	}

	void RingBuffer::jumpToStart()
	{
		m_bufferOffset = 0;
	}

	void RingBuffer::resize(uint64_t size)
	{
		bool wasMapped = m_pRingBuffer;
		if (wasMapped)
		{
			unmap();
		}

		shutdown();
		createBuffer(alignAddress64(size, RESOURCE_PLACEMENT_ALIGNMENT));

		if (wasMapped)
		{
			map();
		}
	}

	void RingBuffer::createBuffer(uint64_t size)
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

		DX_CHECK(m_pDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_pRingBuffer)));

		m_bufferOffset = 0;
		m_maxSize = size;
	}
}
