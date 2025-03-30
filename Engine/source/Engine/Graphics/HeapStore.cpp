#include "HeapStore.h"

namespace Okay
{
	void HeapStore::initialize(ID3D12Device* pDevice, uint64_t staticHeapCreationSize, uint64_t dynamicHeapCreationSize)
	{
		m_pDevice = pDevice;

		// texture alignment aligns for buffers too :3
		m_staticHeapCreationSize = alignAddress64(staticHeapCreationSize, TEXTURE_DATA_PLACEMENT_ALIGNMENT);
		m_dynamicHeapCreationSize = alignAddress64(dynamicHeapCreationSize, TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	}

	void HeapStore::shutdown()
	{
		for (Heap& heap : m_heaps)
		{
			D3D12_RELEASE(heap.pHeap);
		}
		m_heaps.clear();

		m_pDevice = nullptr;
	}
	
	ID3D12Resource* HeapStore::requestResource(D3D12_HEAP_TYPE heapType, uint64_t width, uint32_t height, uint32_t mips, DXGI_FORMAT format, D3D12_CLEAR_VALUE* pClearValue, bool isDepth)
	{
		bool isBuffer = format == DXGI_FORMAT_UNKNOWN;

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = isBuffer ? D3D12_RESOURCE_DIMENSION_BUFFER : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Alignment = RESOURCE_PLACEMENT_ALIGNMENT;

		resourceDesc.Width = width;
		resourceDesc.Height = height;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = mips;

		resourceDesc.Format = format;

		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;

		resourceDesc.Layout = isBuffer ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR : D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = isDepth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_NONE;
		
		/*
			NOTE:
			ID3D12Device::GetCopyableFootprints doesn't get the true total size of textures.
			According to the docs of ID3D12Device::CreatePlacedResource we have to use ID3D12Device::GetResourceAllocationInfo to get the true allocation size of textures
		*/
		D3D12_RESOURCE_ALLOCATION_INFO resourceAllocationInfo = m_pDevice->GetResourceAllocationInfo(0, 1, &resourceDesc);

		D3D12_RESOURCE_STATES initialState = heapType == D3D12_HEAP_TYPE_DEFAULT ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_GENERIC_READ;
		Heap& heap = getSufficientHeap(heapType, resourceAllocationInfo.SizeInBytes);

		ID3D12Resource* pResource = nullptr;
		DX_CHECK(m_pDevice->CreatePlacedResource(heap.pHeap, heap.usedHeapSize, &resourceDesc, initialState, pClearValue, IID_PPV_ARGS(&pResource)));

		heap.usedHeapSize += alignAddress64(resourceAllocationInfo.SizeInBytes, RESOURCE_PLACEMENT_ALIGNMENT);

		return pResource;
	}
	
	Heap& HeapStore::getSufficientHeap(D3D12_HEAP_TYPE heapType, uint64_t requiredSize)
	{
		for (uint64_t i = 0; i < m_heaps.size(); i++)
		{
			if (m_heaps[i].heapType != heapType)
			{
				continue;
			}

			uint64_t heapSize = m_heaps[i].pHeap->GetDesc().SizeInBytes;
			uint64_t availableSize = heapSize - m_heaps[i].usedHeapSize;

			if (requiredSize < availableSize)
			{
				return m_heaps[i];
			}
		}

		uint64_t creationHeapSize = heapType == D3D12_HEAP_TYPE_DEFAULT ? m_staticHeapCreationSize : m_dynamicHeapCreationSize;
		return createNewHeap(heapType, std::max(requiredSize, creationHeapSize));
	}

	Heap& HeapStore::createNewHeap(D3D12_HEAP_TYPE heapType, uint64_t heapSize)
	{
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = heapType;
		heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProperties.CreationNodeMask = 0;
		heapProperties.VisibleNodeMask = 0;

		D3D12_HEAP_DESC heapDesc{};
		heapDesc.SizeInBytes = heapSize;
		heapDesc.Properties = heapProperties;
		heapDesc.Alignment = RESOURCE_PLACEMENT_ALIGNMENT;
		heapDesc.Flags = D3D12_HEAP_FLAG_NONE;

		Heap& heap = m_heaps.emplace_back();
		heap.usedHeapSize = 0;
		heap.heapType = heapType;

		DX_CHECK(m_pDevice->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap.pHeap)));

		return heap;
	}
}
