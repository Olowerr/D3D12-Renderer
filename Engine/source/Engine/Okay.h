#pragma once

#include "glm/glm.hpp"

#include <inttypes.h>
#include <cassert>
#include <cstdio>

// Will be defined to not check in dist builds
#define OKAY_ASSERT(condition)																	\
	{																							\
		if (!(condition))																		\
		{																						\
			printf("ASSERT FAILED: %s\nFile: %s\nLine: %d\n", #condition, __FILE__, __LINE__);	\
			__debugbreak();																		\
		}																						\
	}0

namespace Okay
{
	typedef uint32_t AssetID;

	constexpr uint16_t INVALID_UINT16 = UINT16_MAX;
	constexpr uint32_t INVALID_UINT32 = UINT32_MAX;
	constexpr uint64_t INVALID_UINT64 = UINT64_MAX;



	struct Vertex
	{
		glm::vec3 position = glm::vec3(0.f);
		glm::vec3 normal = glm::vec3(0.f);
		glm::vec2 uv = glm::vec2(0.f);
	};
}
