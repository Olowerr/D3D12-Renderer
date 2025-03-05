#include "DescriptorHeapStore.h"

// Describes the "slots" of uint16_t used within a DescriptorHandle
// Currently assuming each part needs 16 bits
constexpr uint8_t HANDLE_HEAP_IDX_SLOT = 0;
constexpr uint8_t HANDLE_DESCRIPTOR_IDX_SLOT = 1;

namespace Okay
{
	void DescriptorHeapStore::initialize(ID3D12Device* pDevice, uint16_t numSlots)
	{
		m_pDevice = pDevice;
		m_creationSlots = numSlots;
	}

	void DescriptorHeapStore::shutdown()
	{
		for (DescriptorHeap& descriptorHeap : m_descriptorHeaps)
			D3D12_RELEASE(descriptorHeap.pDXDescriptorHeap);

		m_descriptorHeaps.clear();

		m_pDevice = nullptr;
	}

	DescriptorHandle DescriptorHeapStore::createDescriptors(uint32_t numDescriptors, const DescriptorDesc* pDescs, D3D12_DESCRIPTOR_HEAP_TYPE type)
	{
		uint16_t heapIdx = getSufficientDescriptorHeap(numDescriptors, type);
		DescriptorHeap& heap = m_descriptorHeaps[heapIdx];

		D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = heap.pDXDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		descriptorHandle.ptr += (uint64_t)heap.usedDescriptorSlots * heap.incrementSize;

		for (uint32_t i = 0; i < numDescriptors; i++)
		{
			const DescriptorDesc& desc = pDescs[i];

			OKAY_ASSERT(desc.type != OKAY_DESCRIPTOR_TYPE_NONE);

			switch (desc.type)
			{
			case OKAY_DESCRIPTOR_TYPE_SRV:
				m_pDevice->CreateShaderResourceView(desc.pDXResource, desc.nullDesc ? nullptr : &desc.srvDesc, descriptorHandle);
				break;

			case OKAY_DESCRIPTOR_TYPE_CBV:
				m_pDevice->CreateConstantBufferView(desc.nullDesc ? nullptr : &desc.cbvDesc, descriptorHandle);
				break;

			case OKAY_DESCRIPTOR_TYPE_UAV:
				OKAY_ASSERT(false); // TODO: Implement UAV lol (need "counterResource" and I'm unsure atm how that works :thonk:)
				break;

			case OKAY_DESCRIPTOR_TYPE_RTV:
				m_pDevice->CreateRenderTargetView(desc.pDXResource, desc.nullDesc ? nullptr : &desc.rtvDesc, descriptorHandle);
				break;

			case OKAY_DESCRIPTOR_TYPE_DSV:
				m_pDevice->CreateDepthStencilView(desc.pDXResource, desc.nullDesc ? nullptr : &desc.dsvDesc, descriptorHandle);
				break;
			}

			heap.usedDescriptorSlots += 1;
			descriptorHandle.ptr += heap.incrementSize;
		}

		return encodeHandle(heapIdx, (uint16_t)heap.usedDescriptorSlots - numDescriptors);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeapStore::getCPUHandle(DescriptorHandle handle)
	{
		DescriptorHeap* pDescriptorHeap = nullptr;
		uint16_t descriptorIndex = INVALID_UINT16;

		decodeHandle(handle, &pDescriptorHeap, &descriptorIndex);

		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = pDescriptorHeap->pDXDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		cpuHandle.ptr += (uint64_t)descriptorIndex * pDescriptorHeap->incrementSize;

		return cpuHandle;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeapStore::getGPUHandle(DescriptorHandle handle)
	{
		DescriptorHeap* pDescriptorHeap = nullptr;
		uint16_t descriptorIndex = INVALID_UINT16;

		decodeHandle(handle, &pDescriptorHeap, &descriptorIndex);

		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = pDescriptorHeap->pDXDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
		gpuHandle.ptr += (uint64_t)descriptorIndex * pDescriptorHeap->incrementSize;

		return gpuHandle;
	}

	uint16_t DescriptorHeapStore::getSufficientDescriptorHeap(uint16_t slots, D3D12_DESCRIPTOR_HEAP_TYPE type)
	{
		for (uint32_t i = 0; i < (uint32_t)m_descriptorHeaps.size(); i++)
		{
			uint16_t availableSlots = m_descriptorHeaps[i].maxDescriptors - m_descriptorHeaps[i].usedDescriptorSlots;
			if (type == m_descriptorHeaps[i].type && slots <= availableSlots)
			{
				return i;
			}
		}

		return createNewDescriptorHeap(std::max(slots, m_creationSlots), type);
	}

	uint16_t DescriptorHeapStore::createNewDescriptorHeap(uint16_t slots, D3D12_DESCRIPTOR_HEAP_TYPE type)
	{
		DescriptorHeap& heap = m_descriptorHeaps.emplace_back();

		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type = type;
		desc.NumDescriptors = (uint32_t)slots;
		desc.NodeMask = 0;
		desc.Flags = type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? // Unsure if this "should" actually be handled like this :thonk:
			D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		DX_CHECK(m_pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap.pDXDescriptorHeap)));

		heap.maxDescriptors = slots;
		heap.usedDescriptorSlots = 0;
		heap.type = type;
		heap.incrementSize = m_pDevice->GetDescriptorHandleIncrementSize(type);

		return (uint8_t)m_descriptorHeaps.size() - 1;
	}

	DescriptorHandle DescriptorHeapStore::encodeHandle(uint16_t heapIndex, uint16_t descriptorIndex)
	{
		DescriptorHandle handle = 0;
		uint16_t* pHandle = (uint16_t*)&handle;

		pHandle[HANDLE_HEAP_IDX_SLOT] = heapIndex;
		pHandle[HANDLE_DESCRIPTOR_IDX_SLOT] = descriptorIndex;

		return handle;
	}

	void DescriptorHeapStore::decodeHandle(DescriptorHandle handle, DescriptorHeap** ppOutDescriptorHeap, uint16_t* pOutDescriptorIndex)
	{
		uint16_t* pHandle = (uint16_t*)&handle;

		uint16_t heapIndex = pHandle[HANDLE_HEAP_IDX_SLOT];
		uint16_t descriptorIndex = pHandle[HANDLE_DESCRIPTOR_IDX_SLOT];

		validateDecodedHandle(heapIndex, descriptorIndex);

		if (ppOutDescriptorHeap)
			*ppOutDescriptorHeap = &m_descriptorHeaps[heapIndex];

		if (pOutDescriptorIndex)
			*pOutDescriptorIndex = descriptorIndex;
	}

	void DescriptorHeapStore::validateDecodedHandle(uint16_t heapIndex, uint16_t descriptorIndex)
	{
		OKAY_ASSERT(heapIndex < (uint16_t)m_descriptorHeaps.size());

		OKAY_ASSERT(descriptorIndex < (uint16_t)m_descriptorHeaps[heapIndex].usedDescriptorSlots);
	}
}
