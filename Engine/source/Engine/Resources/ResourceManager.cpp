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

	AssetID ResourceManager::loadMesh(FilePath path)
	{
		AssetID id = (AssetID)m_meshes.size();

		MeshData meshData;
		importMeshData(path, meshData);

		m_meshes.emplace_back(meshData);

		return id;
	}

	void ResourceManager::importMeshData(FilePath path, MeshData& outData)
	{
		Assimp::Importer aiImporter;

		const aiScene* pAiScene = aiImporter.ReadFile(path.string(), aiProcess_Triangulate | aiProcess_ConvertToLeftHanded);
		// aiProcess_OptimizeMeshes aiProcess_CalcTangentSpace

		OKAY_ASSERT(pAiScene);
		OKAY_ASSERT(pAiScene->mMeshes[0]);

		const aiMesh& aiMesh = *pAiScene->mMeshes[0];

		outData.verticies.resize(aiMesh.mNumVertices);
		outData.indicies.resize(aiMesh.mNumFaces * 3u);


		// Assuming the same number of positions, normals & uv
		for (uint32_t i = 0; i < aiMesh.mNumVertices; i++)
		{
			outData.verticies[i].position = assimpToGlmVec3(aiMesh.mVertices[i]);
			outData.verticies[i].normal = assimpToGlmVec3(aiMesh.mNormals[i]);
			outData.verticies[i].uv = assimpToGlmVec3(aiMesh.mTextureCoords[0][i]);
		}

		for (uint32_t i = 0; i < aiMesh.mNumFaces; i++)
		{
			outData.indicies[i * 3 ] = aiMesh.mFaces[i].mIndices[0];
			outData.indicies[i * 3 + 1] = aiMesh.mFaces[i].mIndices[1];
			outData.indicies[i * 3 + 2] = aiMesh.mFaces[i].mIndices[2];
		}
	}
}
