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

		inline const MeshData& getMeshData() const
		{
			return m_meshData;
		}

		inline void clearData()
		{
			m_meshData.verticies.resize(0);
			m_meshData.indicies.resize(0);
		}

	private:
		MeshData m_meshData;

	};
}
