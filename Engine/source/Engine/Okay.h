#pragma once

#include "glm/glm.hpp"

#include <inttypes.h>
#include <cassert>

// Will be defined to not check in dist builds
#define OKAY_ASSERT(condition)																	\
	{																							\
		if (!(condition))																		\
		{																						\
			printf("ASSERT FAILED: %s\nFile: %s\nLine: %d\n", #condition, __FILE__, __LINE__);	\
			__debugbreak();																		\
		}																						\
	}0

// Always defined to check condition
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

namespace Okay
{
	struct Vertex
	{
		glm::vec3 position;
	};

	constexpr uint32_t INVALID_UINT = UINT32_MAX;
}
