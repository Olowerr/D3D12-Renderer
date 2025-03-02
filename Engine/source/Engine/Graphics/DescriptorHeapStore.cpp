#include "DescriptorHeapStore.h"

namespace Okay
{
	void DescriptorHeapStore::initialize(ID3D12Device* pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numSlots)
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

	uint32_t DescriptorHeapStore::createDescriptors(uint32_t numDescriptors, const DescriptorDesc* pDescs)
	{
		DescriptorHeap& heap = getSufficientDescriptorHeap(numDescriptors);

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

		// return start slot idx
		return heap.usedDescriptorSlots - numDescriptors;
	}

	DescriptorHeap& DescriptorHeapStore::getSufficientDescriptorHeap(uint32_t slots)
	{
		for (uint32_t i = 0; i < (uint32_t)m_descriptorHeaps.size(); i++)
		{
			if (slots <= m_descriptorHeaps[i].maxDescriptors - m_descriptorHeaps[i].usedDescriptorSlots)
			{
				return m_descriptorHeaps[i];
			}
		}

		return createNewDescriptorHeap(std::max(slots, m_creationSlots));
	}

	DescriptorHeap& DescriptorHeapStore::createNewDescriptorHeap(uint32_t slots)
	{
		DescriptorHeap& heap = m_descriptorHeaps.emplace_back();

		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type = m_type;
		desc.NumDescriptors = slots;
		desc.NodeMask = 0;
		desc.Flags = m_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ?
			D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		DX_CHECK(m_pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap.pDXDescriptorHeap)));

		heap.maxDescriptors = slots;
		heap.usedDescriptorSlots = 0;

		return heap;
	}
}
