#pragma once

#include "Okay.h"
#include "entt/entt.hpp"

#include <d3d12.h>
#include <dxgi1_2.h>
#include <dxgidebug.h>
#include <d3dcompiler.h>
#include <dxgi.h>

// Always defined to check condition, TODO: Rename marco to like "OKAY_ALWAYS_ASSERT", or even better move this define to Okay.h and make OKAY_ASSERT use it
#define OKAY_ASSERT2(condition)																	\
	{																							\
		if (!(condition))																		\
		{																						\
			printf("ASSERT FAILED: %s\nFile: %s\nLine: %d\n", #condition, __FILE__, __LINE__);	\
			__debugbreak();																		\
		}																						\
	}0


#define DX_CHECK(x) OKAY_ASSERT2(SUCCEEDED(x))
#define D3D12_RELEASE(x) if (x) { x->Release(); x = nullptr; }0

#define RESOURCE_PLACEMENT_ALIGNMENT		D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
#define BUFFER_DATA_ALIGNMENT				D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT
#define TEXTURE_DATA_PLACEMENT_ALIGNMENT	D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT
#define TEXTURE_DATA_PITCH_ALIGNMENT		D3D12_TEXTURE_DATA_PITCH_ALIGNMENT

namespace Okay
{
	typedef uint16_t ResourceHandle;
	constexpr ResourceHandle INVALID_RH = INVALID_UINT16;

	inline const FilePath SHADER_PATH = FilePath("..") / "Engine" / "resources" / "shaders";

	enum TextureFlags : uint32_t
	{
		OKAY_TEXTURE_FLAG_NONE = 0,
		OKAY_TEXTURE_FLAG_RENDER = 1,
		OKAY_TEXTURE_FLAG_SHADER_READ = 2,
		OKAY_TEXTURE_FLAG_DEPTH = 4,
		OKAY_TEXTURE_FLAG_CUBE = 8,
	};

	struct Allocation
	{
		ResourceHandle resourceHandle = INVALID_RH;
		ID3D12Resource* pDXResource = nullptr;

		uint64_t resourceOffset = INVALID_UINT64;

		uint64_t elementSize = INVALID_UINT64;
		uint32_t numElements = INVALID_UINT32;
	};

	struct Resource
	{
		ResourceHandle handle = INVALID_RH;
		ID3D12Resource* pDXResource = nullptr;

		D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE(-1);

		uint64_t maxSize = INVALID_UINT64;
		uint64_t nextAppendOffset = INVALID_UINT64;
	};

	struct DXMesh
	{
		DXMesh() = default;

		D3D12_GPU_VIRTUAL_ADDRESS gpuVerticiesGVA = {};
		D3D12_INDEX_BUFFER_VIEW indiciesView = {};
		uint32_t numIndicies = INVALID_UINT32;
	};

	struct DrawGroup
	{
		DrawGroup() = default;

		uint32_t dxMeshId = INVALID_UINT32;
		std::vector<entt::entity> entities;

		D3D12_GPU_VIRTUAL_ADDRESS objectDatasVA = INVALID_UINT64;
	};

	struct ShadowMap
	{
		Allocation textureAllocation = {};
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = {};

		glm::mat4 viewProjMatrices[6] = {};
		glm::vec3 lightPos = glm::vec3(0.f);
	};

	enum DescriptorType : uint32_t
	{
		OKAY_DESCRIPTOR_TYPE_NONE = 0,
		OKAY_DESCRIPTOR_TYPE_SRV = 1,
		OKAY_DESCRIPTOR_TYPE_CBV = 2,
		OKAY_DESCRIPTOR_TYPE_UAV = 3,
		OKAY_DESCRIPTOR_TYPE_RTV = 4,
		OKAY_DESCRIPTOR_TYPE_DSV = 5,
	};

	struct DescriptorDesc
	{
		DescriptorType type = OKAY_DESCRIPTOR_TYPE_NONE;
		union
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;
			D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
		};

		bool nullDesc = true; // convenience, true means nullptr desc argument during creation
		ID3D12Resource* pDXResource = nullptr; // Some views need the resource for creation
	};

	constexpr uint64_t alignAddress64(uint64_t adress, uint32_t alignment)
	{
		return ((adress - 1) - ((adress - 1) % alignment)) + alignment;
	}

	constexpr uint32_t alignAddress32(uint32_t address, uint32_t alignment)
	{
		return (uint32_t)alignAddress64((uint64_t)address, alignment);
	}

	inline void logAdapterInfo(IDXGIAdapter* pAdapter)
	{
		DXGI_ADAPTER_DESC adapterDesc{};
		pAdapter->GetDesc(&adapterDesc);

		// https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo

		printf("-- GPU INFO --\n");
		printf("Name: %ws\n", adapterDesc.Description);
		printf("Dedicated Video Memory: %.2f GB\n", adapterDesc.DedicatedVideoMemory / 1'000'000'000.f);
		printf("Dedicated System Memory: %.2f GB\n", adapterDesc.DedicatedSystemMemory / 1'000'000'000.f);
		printf("Shared System Memory: %.2f GB\n\n", adapterDesc.SharedSystemMemory / 1'000'000'000.f);
	}

	class IncludeReader : public ID3DInclude
	{
	public:
		// Inherited via ID3DInclude
		virtual HRESULT __stdcall Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID* ppData, UINT* pBytes) override
		{
			if (!Okay::readBinary(SHADER_PATH / pFileName, m_includeBuffer))
			{
				return E_FAIL;
			}

			*ppData = m_includeBuffer.c_str();
			*pBytes = (uint32_t)m_includeBuffer.size();

			return S_OK;
		}

		virtual HRESULT __stdcall Close(LPCVOID pData) override
		{
			m_includeBuffer.clear();
			m_includeBuffer.shrink_to_fit();
			return S_OK;
		}

	private:
		std::string m_includeBuffer;
	};

	inline D3D12_SHADER_BYTECODE compileShader(FilePath path, std::string_view version, ID3DBlob** pShaderBlob)
	{
		ID3DBlob* pErrorBlob = nullptr;

#ifdef _DEBUG
		uint32_t flags1 = D3DCOMPILE_DEBUG;
#else
		uint32_t flags1 = D3DCOMPILE_OPTIMIZATION_LEVEL2;
#endif

		IncludeReader includer;

		HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, &includer, "main", version.data(), flags1, 0, pShaderBlob, &pErrorBlob);

		if (FAILED(hr))
		{
			const char* pErrorMsg = pErrorBlob ? (const char*)pErrorBlob->GetBufferPointer() : "No errors produced, file might not have been found.";
			printf("Shader Compilation failed:\n%s\n", pErrorMsg);
			OKAY_ASSERT(false);
		}

		if (pErrorBlob)
		{
			const char* pErrorMsg = (const char*)pErrorBlob->GetBufferPointer();
			printf("Shader Compilation message:\n%s\n", pErrorMsg);
		}

		D3D12_RELEASE(pErrorBlob);

		D3D12_SHADER_BYTECODE shaderByteCode{};
		shaderByteCode.pShaderBytecode = (*pShaderBlob)->GetBufferPointer();
		shaderByteCode.BytecodeLength = (*pShaderBlob)->GetBufferSize();

		return shaderByteCode;
	}

	inline ID3D12RootSignature* createRootSignature(ID3D12Device* pDevice, const D3D12_ROOT_SIGNATURE_DESC& rootSignatureDesc)
	{
		ID3DBlob* pRootBlob = nullptr;
		ID3DBlob* pErrorBlob = nullptr;

		HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &pRootBlob, &pErrorBlob);
		if (FAILED(hr))
		{
			printf("Failed to serialize root signature: %s\n", pErrorBlob ? (char*)pErrorBlob->GetBufferPointer() : "No errors produced.");
			OKAY_ASSERT(false);
		}

		ID3D12RootSignature* pRootSignature = nullptr;
		DX_CHECK(pDevice->CreateRootSignature(0, pRootBlob->GetBufferPointer(), pRootBlob->GetBufferSize(), IID_PPV_ARGS(&pRootSignature)));

		D3D12_RELEASE(pRootBlob);
		D3D12_RELEASE(pErrorBlob);

		return pRootSignature;
	}

	inline void copyDifferentTextures(ID3D12GraphicsCommandList* pCommandList, ID3D12Resource* pDest, ID3D12Resource* pSource, uint32_t srcWidth, uint32_t srcHeight)
	{
		D3D12_TEXTURE_COPY_LOCATION destLocation = {};
		destLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		destLocation.pResource = pDest;

		D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
		srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		srcLocation.pResource = pSource;

		D3D12_BOX sourceBox = {};
		sourceBox.left = 0;
		sourceBox.top = 0;
		sourceBox.right = srcWidth;
		sourceBox.bottom = srcHeight;
		sourceBox.front = 0;
		sourceBox.back = 1;

		uint32_t minMips = glm::min(pDest->GetDesc().MipLevels, pSource->GetDesc().MipLevels);

		for (uint32_t i = 0; i < minMips; i++)
		{
			destLocation.SubresourceIndex = i;
			srcLocation.SubresourceIndex = i;

			pCommandList->CopyTextureRegion(&destLocation, 0, 0, 0, &srcLocation, &sourceBox);

			sourceBox.right = glm::max(sourceBox.right / 2, 1u);
			sourceBox.bottom = glm::max(sourceBox.bottom / 2, 1u);
		}
	}

	constexpr D3D12_ROOT_PARAMETER createRootParamCBV(D3D12_SHADER_VISIBILITY visibility, uint32_t shaderRegister, uint32_t registerSpace)
	{
		D3D12_ROOT_PARAMETER param = {};
		param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		param.ShaderVisibility = visibility;
		param.Descriptor.ShaderRegister = shaderRegister;
		param.Descriptor.RegisterSpace = registerSpace;
		return param;
	}

	constexpr D3D12_ROOT_PARAMETER createRootParamSRV(D3D12_SHADER_VISIBILITY visibility, uint32_t shaderRegister, uint32_t registerSpace)
	{
		D3D12_ROOT_PARAMETER param = {};
		param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		param.ShaderVisibility = visibility;
		param.Descriptor.ShaderRegister = shaderRegister;
		param.Descriptor.RegisterSpace = registerSpace;
		return param;
	}

	constexpr D3D12_ROOT_PARAMETER createRootParamUAV(D3D12_SHADER_VISIBILITY visibility, uint32_t shaderRegister, uint32_t registerSpace)
	{
		D3D12_ROOT_PARAMETER param = {};
		param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		param.ShaderVisibility = visibility;
		param.Descriptor.ShaderRegister = shaderRegister;
		param.Descriptor.RegisterSpace = registerSpace;
		return param;
	}

	constexpr D3D12_ROOT_PARAMETER createRootParamConstants(D3D12_SHADER_VISIBILITY visibility, uint32_t shaderRegister, uint32_t registerSpace, uint32_t numValues)
	{
		D3D12_ROOT_PARAMETER param = {};
		param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		param.ShaderVisibility = visibility;
		param.Constants.Num32BitValues = numValues;
		param.Constants.ShaderRegister = shaderRegister;
		param.Constants.RegisterSpace = registerSpace;
		return param;
	}

	constexpr D3D12_ROOT_PARAMETER createRootParamTable(D3D12_SHADER_VISIBILITY visibility, D3D12_DESCRIPTOR_RANGE* pRanges, uint32_t numRanges)
	{
		D3D12_ROOT_PARAMETER param = {};
		param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		param.DescriptorTable.NumDescriptorRanges = numRanges;
		param.DescriptorTable.pDescriptorRanges = pRanges;
		return param;
	}

	constexpr D3D12_DESCRIPTOR_RANGE createRangeCBV(uint32_t shaderRegister, uint32_t registerSpace, uint32_t numDescriptors, uint32_t offset)
	{
		D3D12_DESCRIPTOR_RANGE range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		range.NumDescriptors = numDescriptors;
		range.BaseShaderRegister = shaderRegister;
		range.RegisterSpace = registerSpace;
		range.OffsetInDescriptorsFromTableStart = offset;
		return range;
	}

	constexpr D3D12_DESCRIPTOR_RANGE createRangeSRV(uint32_t shaderRegister, uint32_t registerSpace, uint32_t numDescriptors, uint32_t offset)
	{
		D3D12_DESCRIPTOR_RANGE range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range.NumDescriptors = numDescriptors;
		range.BaseShaderRegister = shaderRegister;
		range.RegisterSpace = registerSpace;
		range.OffsetInDescriptorsFromTableStart = offset;
		return range;
	}

	constexpr D3D12_DESCRIPTOR_RANGE createRangeUAV(uint32_t shaderRegister, uint32_t registerSpace, uint32_t numDescriptors, uint32_t offset)
	{
		D3D12_DESCRIPTOR_RANGE range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		range.NumDescriptors = numDescriptors;
		range.BaseShaderRegister = shaderRegister;
		range.RegisterSpace = registerSpace;
		range.OffsetInDescriptorsFromTableStart = offset;
		return range;
	}

	constexpr D3D12_DESCRIPTOR_RANGE createRangeSampler(uint32_t shaderRegister, uint32_t registerSpace, uint32_t numDescriptors, uint32_t offset)
	{
		D3D12_DESCRIPTOR_RANGE range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		range.NumDescriptors = numDescriptors;
		range.BaseShaderRegister = shaderRegister;
		range.RegisterSpace = registerSpace;
		range.OffsetInDescriptorsFromTableStart = offset;
		return range;
	}

	constexpr D3D12_VIEWPORT createViewport(float width, float height)
	{
		D3D12_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.f;
		viewport.TopLeftY = 0.f;
		viewport.Width = width;
		viewport.Height = height;
		viewport.MinDepth = 0;
		viewport.MaxDepth = 1;

		return viewport;
	}

	constexpr D3D12_RECT createRect(uint32_t width, uint32_t height)
	{
		D3D12_RECT rect = {};
		rect.left = 0;
		rect.top = 0;
		rect.right = (LONG)width;
		rect.bottom = (LONG)height;

		return rect;
	}

	constexpr D3D12_BLEND_DESC createDefaultBlendDesc()
	{
		D3D12_BLEND_DESC desc = {};
		desc.AlphaToCoverageEnable = false;
		desc.IndependentBlendEnable = false;

		D3D12_RENDER_TARGET_BLEND_DESC rtvBlendDesc = {};
		rtvBlendDesc.BlendEnable = false;
		rtvBlendDesc.LogicOpEnable = false;
		rtvBlendDesc.SrcBlend = D3D12_BLEND_ONE;
		rtvBlendDesc.DestBlend = D3D12_BLEND_ZERO;
		rtvBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
		rtvBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
		rtvBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
		rtvBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		rtvBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
		rtvBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		desc.RenderTarget[0] = rtvBlendDesc;
		desc.RenderTarget[1] = rtvBlendDesc;
		desc.RenderTarget[2] = rtvBlendDesc;
		desc.RenderTarget[3] = rtvBlendDesc;
		desc.RenderTarget[4] = rtvBlendDesc;
		desc.RenderTarget[5] = rtvBlendDesc;
		desc.RenderTarget[6] = rtvBlendDesc;
		desc.RenderTarget[7] = rtvBlendDesc;

		return desc;
	}

	constexpr D3D12_RASTERIZER_DESC createDefaultRasterizerDesc()
	{
		D3D12_RASTERIZER_DESC desc = {};
		desc.FillMode = D3D12_FILL_MODE_SOLID;
		desc.CullMode = D3D12_CULL_MODE_BACK;
		desc.FrontCounterClockwise = false;
		desc.DepthBias = 0;
		desc.DepthBiasClamp = 0.0f;
		desc.SlopeScaledDepthBias = 0.0f;
		desc.DepthClipEnable = true;
		desc.MultisampleEnable = false;
		desc.AntialiasedLineEnable = false;
		desc.ForcedSampleCount = 0;
		desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		return desc;
	}

	constexpr D3D12_DEPTH_STENCIL_DESC createDefaultDepthStencilDesc()
	{
		D3D12_DEPTH_STENCIL_DESC desc = {};
		desc.DepthEnable = true;
		desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

		desc.StencilEnable = false;
		desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
		desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

		desc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		desc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		desc.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		desc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

		desc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		desc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		desc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		desc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

		return desc;
	}

	constexpr D3D12_GRAPHICS_PIPELINE_STATE_DESC createDefaultGraphicsPipelineStateDesc()
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.StreamOutput.pSODeclaration = nullptr;
		desc.StreamOutput.NumEntries = 0;
		desc.StreamOutput.pBufferStrides = nullptr;
		desc.StreamOutput.NumStrides = 0;
		desc.StreamOutput.RasterizedStream = 0;

		desc.BlendState = createDefaultBlendDesc();
		desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		desc.RasterizerState = createDefaultRasterizerDesc();
		desc.DepthStencilState = createDefaultDepthStencilDesc();

		desc.InputLayout.pInputElementDescs = nullptr;
		desc.InputLayout.NumElements = 0;
		desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;

		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		desc.NumRenderTargets = 0;
		desc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
		desc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
		desc.RTVFormats[2] = DXGI_FORMAT_UNKNOWN;
		desc.RTVFormats[3] = DXGI_FORMAT_UNKNOWN;
		desc.RTVFormats[4] = DXGI_FORMAT_UNKNOWN;
		desc.RTVFormats[5] = DXGI_FORMAT_UNKNOWN;
		desc.RTVFormats[6] = DXGI_FORMAT_UNKNOWN;
		desc.RTVFormats[7] = DXGI_FORMAT_UNKNOWN;

		desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;

		desc.NodeMask = 0;

		desc.CachedPSO.CachedBlobSizeInBytes = 0;
		desc.CachedPSO.pCachedBlob = nullptr;

		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		return desc;
	}

	constexpr D3D12_STATIC_SAMPLER_DESC createDefaultStaticPointSamplerDesc()
	{
		D3D12_STATIC_SAMPLER_DESC desc = {};
		desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
		
		desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

		desc.MipLODBias = 0.f;
		desc.MaxAnisotropy = 1;
		desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; //?
		desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
		
		desc.MinLOD = 0;
		desc.MaxLOD = D3D12_FLOAT32_MAX;

		desc.ShaderRegister = 0;
		desc.RegisterSpace = 0;

		desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		return desc;
	}

}