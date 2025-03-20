#include "ResourceManager.h"

#include "assimp/importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

namespace Okay
{
	static glm::vec3 assimpToGlmVec3(const aiVector3D& vector)
	{
		return glm::vec3(vector.x, vector.y, vector.z);
	}

	AssetID ResourceManager::loadMesh(std::string_view path)
	{
		AssetID id = (AssetID)m_meshes.size();

		MeshData meshData;
		importMeshData(path, meshData);

		m_meshes.emplace_back(meshData);

		return id;
	}

	void ResourceManager::importMeshData(std::string_view path, MeshData& outData)
	{
		Assimp::Importer aiImporter;

		const aiScene* pAiScene = aiImporter.ReadFile(path.data(), aiProcess_Triangulate | aiProcess_ConvertToLeftHanded);
		// aiProcess_OptimizeMeshes aiProcess_CalcTangentSpace

		OKAY_ASSERT(pAiScene);
		OKAY_ASSERT(pAiScene->mMeshes[0]);

		const aiMesh& aiMesh = *pAiScene->mMeshes[0];

		uint32_t numVerticies = aiMesh.mNumFaces * 3u;
		outData.verticies.resize(numVerticies);

		bool hasUV = aiMesh.HasTextureCoords(0u);
		bool hasTangent = aiMesh.HasTangentsAndBitangents();

		for (uint32_t i = 0; i < aiMesh.mNumFaces; i++)
		{
			uint32_t vertex0Idx = i * 3;
			uint32_t* aiIndices = aiMesh.mFaces[i].mIndices;

			for (uint32_t j = 0; j < 3; j++)
			{
				glm::vec3 position = assimpToGlmVec3(aiMesh.mVertices[aiIndices[j]]);
				glm::vec3 normal = assimpToGlmVec3(aiMesh.mNormals[aiIndices[j]]);
				glm::vec2 uv = hasUV ? assimpToGlmVec3(aiMesh.mTextureCoords[0][aiIndices[j]]) : glm::vec2(0.f);

				uint32_t vertexIdx = vertex0Idx + j;

				outData.verticies[vertexIdx].position = position;
				outData.verticies[vertexIdx].normal = normal;
				outData.verticies[vertexIdx].uv = uv;
			}
		}
	}
}
