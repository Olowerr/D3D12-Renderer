#pragma once

#include "Engine/Okay.h"

#include <vector>

namespace Okay
{
	struct MeshData
	{
		std::vector<Vertex> verticies;
		std::vector<uint32_t> indicies;
	};

	class Mesh
	{
	public:
		Mesh() = default;
		Mesh(const MeshData& meshData)
			:m_meshData(meshData)
		{
		}

		virtual ~Mesh() = default;

		inline const MeshData& getMeshData()
		{
			return m_meshData;
		}

	private:

		MeshData m_meshData;

	};
}
