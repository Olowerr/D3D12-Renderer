#include "DescriptorHeapStore.h"

// Describes the "slots" of uint16_t used within a DescriptorHandle
// Currently assuming each part needs 16 bits
constexpr uint8_t HANDLE_HEAP_IDX_SLOT = 0;
constexpr uint8_t HANDLE_DESCRIPTOR_IDX_SLOT = 1;

namespace Okay
{
	void DescriptorHeapStore::initialize(ID3D12Device* pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type, uint16_t numSlots)
	{
		m_pDevice = pDevice;
		m_type = type;
		m_creationSlots = numSlots;

		m_incrementSize = pDevice->GetDescriptorHandleIncrementSize(type);
	}

	void DescriptorHeapStore::shutdown()
	{
		for (DescriptorHeap& descriptorHeap : m_descriptorHeaps)
			D3D12_RELEASE(descriptorHeap.pDXDescriptorHeap);

		m_descriptorHeaps.clear();

		m_pDevice = nullptr;
	}

	DescriptorHandle DescriptorHeapStore::createDescriptors(uint32_t numDescriptors, const DescriptorDesc* pDescs)
	{
		uint16_t heapIdx = getSufficientDescriptorHeap(numDescriptors);
		DescriptorHeap& heap = m_descriptorHeaps[heapIdx];

		D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = heap.pDXDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		descriptorHandle.ptr += (uint64_t)heap.usedDescriptorSlots * m_incrementSize;

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
			descriptorHandle.ptr += m_incrementSize;
		}

		return encodeHandle(heapIdx, (uint16_t)heap.usedDescriptorSlots - numDescriptors);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeapStore::getCPUHandle(DescriptorHandle handle)
	{
		DescriptorHeap* pDewscriptorHeao = nullptr;
		uint16_t descriptorIndex = INVALID_UINT16;

		decodeHandle(handle, &pDewscriptorHeao, &descriptorIndex);

		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = pDewscriptorHeao->pDXDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		cpuHandle.ptr += (uint64_t)descriptorIndex * m_incrementSize;

		return cpuHandle;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeapStore::getGPUHandle(DescriptorHandle handle)
	{
		DescriptorHeap* pDewscriptorHeao = nullptr;
		uint16_t descriptorIndex = INVALID_UINT16;

		decodeHandle(handle, &pDewscriptorHeao, &descriptorIndex);

		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = pDewscriptorHeao->pDXDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
		gpuHandle.ptr += (uint64_t)descriptorIndex * m_incrementSize;

		return gpuHandle;
	}

	uint16_t DescriptorHeapStore::getSufficientDescriptorHeap(uint16_t slots)
	{
		for (uint32_t i = 0; i < (uint32_t)m_descriptorHeaps.size(); i++)
		{
			if (slots <= m_descriptorHeaps[i].maxDescriptors - m_descriptorHeaps[i].usedDescriptorSlots)
			{
				return i;
			}
		}

		return createNewDescriptorHeap(std::max(slots, m_creationSlots));
	}

	uint16_t DescriptorHeapStore::createNewDescriptorHeap(uint16_t slots)
	{
		DescriptorHeap& heap = m_descriptorHeaps.emplace_back();

		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type = m_type;
		desc.NumDescriptors = (uint32_t)slots;
		desc.NodeMask = 0;
		desc.Flags = m_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ?
			D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		DX_CHECK(m_pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap.pDXDescriptorHeap)));

		heap.maxDescriptors = slots;
		heap.usedDescriptorSlots = 0;

		return (uint8_t)m_descriptorHeaps.size() - 1;
	}

	DescriptorHandle DescriptorHeapStore::encodeHandle(uint16_t heapindex, uint16_t descriptorIndex)
	{
		DescriptorHandle handle = 0;
		uint16_t* pHandle = (uint16_t*)&handle;

		pHandle[HANDLE_HEAP_IDX_SLOT] = heapindex;
		pHandle[HANDLE_DESCRIPTOR_IDX_SLOT] = heapindex;

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
			*pOutDescriptorIndex = pHandle[descriptorIndex];
	}

	void DescriptorHeapStore::validateDecodedHandle(uint16_t heapIndex, uint16_t descriptorIndex)
	{
		OKAY_ASSERT(heapIndex < (uint16_t)m_descriptorHeaps.size());

		OKAY_ASSERT(descriptorIndex < (uint16_t)m_descriptorHeaps[heapIndex].usedDescriptorSlots);
	}
}
