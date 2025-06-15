#include "DescriptorHeapStore.h"

namespace Okay
{
	void DescriptorHeapStore::initialize(ID3D12Device* pDevice, uint32_t committedHeapStdSize)
	{
		m_pDevice = pDevice;
		m_committedHeapStdSize = committedHeapStdSize;
	}

	void DescriptorHeapStore::shutdown()
	{
		for (DescriptorHeap& descriptorHeap : m_descriptorHeaps)
			D3D12_RELEASE(descriptorHeap.pDXDescriptorHeap);

		m_descriptorHeaps.clear();

		m_pDevice = nullptr;
	}

	DescriptorHeapHandle DescriptorHeapStore::createDescriptorHeap(uint32_t numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible)
	{
		return createDescriptorHeap_Internal(numDescriptors, type, shaderVisible, false);
	}

	Descriptor DescriptorHeapStore::allocateDescriptors(DescriptorHeapHandle heapHandle, uint32_t slotOffset, const DescriptorDesc* pDescs, uint32_t numDescriptors)
	{
		validateDescriptorHeapHandle(heapHandle);
		DescriptorHeap& heap = m_descriptorHeaps[heapHandle];

		if (slotOffset == OKAY_DESCRIPTOR_APPEND)
		{
			slotOffset = heap.nextAppendSlot;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = heap.pDXDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		descriptorHandle.ptr += (uint64_t)slotOffset * heap.incrementSize;

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
				m_pDevice->CreateUnorderedAccessView(desc.pDXResource, nullptr, desc.nullDesc ? nullptr : &desc.uavDesc, descriptorHandle);
				break;

			case OKAY_DESCRIPTOR_TYPE_RTV:
				m_pDevice->CreateRenderTargetView(desc.pDXResource, desc.nullDesc ? nullptr : &desc.rtvDesc, descriptorHandle);
				break;

			case OKAY_DESCRIPTOR_TYPE_DSV:
				m_pDevice->CreateDepthStencilView(desc.pDXResource, desc.nullDesc ? nullptr : &desc.dsvDesc, descriptorHandle);
				break;
			}

			descriptorHandle.ptr += heap.incrementSize;
		}

		heap.nextAppendSlot += numDescriptors;

		Descriptor descriptor = {};
		descriptor.heapHandle = heapHandle;
		descriptor.heapSlot = slotOffset;

		descriptor.cpuHandle = heap.pDXDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		descriptor.gpuHandle = heap.pDXDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

		descriptor.cpuHandle.ptr += (uint64_t)heap.incrementSize * descriptor.heapSlot;
		descriptor.gpuHandle.ptr += (uint64_t)heap.incrementSize * descriptor.heapSlot;

		return descriptor;
	}

	Descriptor DescriptorHeapStore::allocateCommittedDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, const DescriptorDesc* pDescs, uint32_t numDescriptors)
	{
		DescriptorHeapHandle heapHandle = findSufficentCommittedHeap(numDescriptors, type);
		return allocateDescriptors(heapHandle, OKAY_DESCRIPTOR_APPEND, pDescs, numDescriptors);
	}

	ID3D12DescriptorHeap* DescriptorHeapStore::getDXDescriptorHeap(DescriptorHeapHandle heapHandle)
	{
		validateDescriptorHeapHandle(heapHandle);
		return m_descriptorHeaps[heapHandle].pDXDescriptorHeap;
	}

	DescriptorHeapHandle DescriptorHeapStore::findSufficentCommittedHeap(uint32_t numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type)
	{
		for (uint32_t i = 0; i < (uint32_t)m_descriptorHeaps.size(); i++)
		{
			const DescriptorHeap& heap = m_descriptorHeaps[i];
			if (!heap.committed || heap.type != type)
			{
				continue;
			}

			if (heap.committed && numDescriptors <= heap.maxDescriptors - heap.nextAppendSlot)
			{
				return (DescriptorHeapHandle)i;
			}
		}

		return createDescriptorHeap_Internal(std::max(m_committedHeapStdSize, numDescriptors), type, false, true);
	}

	DescriptorHeapHandle DescriptorHeapStore::createDescriptorHeap_Internal(uint32_t numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible, bool committed)
	{
		DescriptorHeap& heap = m_descriptorHeaps.emplace_back();
		heap.type = type;
		heap.maxDescriptors = numDescriptors;
		heap.nextAppendSlot = 0;
		heap.incrementSize = m_pDevice->GetDescriptorHandleIncrementSize(type);
		heap.committed = committed;

		D3D12_DESCRIPTOR_HEAP_DESC dxHeapDesc = {};
		dxHeapDesc.Type = type;
		dxHeapDesc.NumDescriptors = numDescriptors;
		dxHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		dxHeapDesc.NodeMask = 0;

		DX_CHECK(m_pDevice->CreateDescriptorHeap(&dxHeapDesc, IID_PPV_ARGS(&heap.pDXDescriptorHeap)));

		return DescriptorHeapHandle(m_descriptorHeaps.size() - 1);
	}

	void DescriptorHeapStore::validateDescriptorHeapHandle(DescriptorHeapHandle handle)
	{
		OKAY_ASSERT(handle < (DescriptorHeapHandle)m_descriptorHeaps.size());
	}
}
