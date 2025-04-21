#include "ResourceManager.h"

#include "assimp/importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include <stack>

namespace Okay
{
	static glm::vec3 assimpToGlmVec3(const aiVector3D& vector)
	{
		return glm::vec3(vector.x, vector.y, vector.z);
	}

	static glm::mat4 assimpToGlmMat4(const aiMatrix4x4& matrix)
	{
		glm::mat4 glmMat = {};
		glmMat[0] = glm::vec4(matrix.a1, matrix.a2, matrix.a3, matrix.a4);
		glmMat[1] = glm::vec4(matrix.b1, matrix.b2, matrix.b3, matrix.b4);
		glmMat[2] = glm::vec4(matrix.c1, matrix.c2, matrix.c3, matrix.c4);
		glmMat[3] = glm::vec4(matrix.d1, matrix.d2, matrix.d3, matrix.d4);

		return glmMat;
	}

	static void convertMeshData(aiMesh* pAiMesh, MeshData& outData, float scale)
	{
		bool hasUV = pAiMesh->HasTextureCoords(0);
		bool hasTangent = pAiMesh->HasTangentsAndBitangents();

		outData.verticies.resize(pAiMesh->mNumVertices);
		outData.indicies.resize(pAiMesh->mNumFaces * 3ull);

		// Assuming the same number of positions, normals, uv, etc
		for (uint32_t i = 0; i < pAiMesh->mNumVertices; i++)
		{
			outData.verticies[i].position = assimpToGlmVec3(pAiMesh->mVertices[i]) * scale;
			outData.verticies[i].normal = assimpToGlmVec3(pAiMesh->mNormals[i]);
			outData.verticies[i].uv = hasUV ? assimpToGlmVec3(pAiMesh->mTextureCoords[0][i]) : glm::vec2(0.f);
			if (hasTangent)
			{
				outData.verticies[i].tangent = assimpToGlmVec3(pAiMesh->mTangents[i]);
				outData.verticies[i].biTangent = assimpToGlmVec3(pAiMesh->mBitangents[i]);
			} // Unsure atm what happens tot he normal calculation if tangent & biTangent are the default zero vec3
		}

		for (uint32_t i = 0; i < pAiMesh->mNumFaces; i++)
		{
			outData.indicies[i * 3ull] = pAiMesh->mFaces[i].mIndices[0];
			outData.indicies[i * 3ull + 1] = pAiMesh->mFaces[i].mIndices[1];
			outData.indicies[i * 3ull + 2] = pAiMesh->mFaces[i].mIndices[2];
		}
	}

	static void importMeshData(FilePath path, MeshData& outData)
	{
		Assimp::Importer aiImporter;

		const aiScene* pAiScene = aiImporter.ReadFile(path.string(), aiProcess_Triangulate | aiProcess_ConvertToLeftHanded);
		// aiProcess_OptimizeMeshes 

		OKAY_ASSERT(pAiScene);
		OKAY_ASSERT(pAiScene->mMeshes[0]);

		convertMeshData(pAiScene->mMeshes[0], outData, 1.f);
	}

	static void findOrLoadTexture(aiMaterial* pAiMaterial, aiTextureType textureType, std::unordered_map<std::string, AssetID>& loadedTextures, FilePath folderPath, ResourceManager* pResourceManager, AssetID& outAssetID)
	{
		aiString texturePath;

		if (pAiMaterial->GetTexture(textureType, 0u, &texturePath) == aiReturn_SUCCESS)
		{
			if (loadedTextures.contains(texturePath.C_Str()))
			{
				outAssetID = loadedTextures[texturePath.C_Str()];
			}
			else
			{
				outAssetID = pResourceManager->loadTexture(folderPath / texturePath.C_Str());
				loadedTextures[texturePath.C_Str()] = outAssetID;
			}
		}
		else
		{
			outAssetID = 0; // Should use some default texture, but just picking one is fine for now :] (this will be funky for normalMaps :eyes:)
		}
	}

	AssetID ResourceManager::loadMesh(FilePath path)
	{
		AssetID id = (AssetID)m_meshes.size();

		MeshData meshData;
		importMeshData(path, meshData);

		m_meshes.emplace_back(meshData);

		return id;
	}

	AssetID ResourceManager::loadTexture(FilePath path)
	{
		AssetID id = (AssetID)m_textures.size();

		int width = 0, height = 0;
		uint8_t* pData = stbi_load(path.string().c_str(), &width, &height, nullptr, STBI_rgb_alpha);

		OKAY_ASSERT(pData);

		Texture& texture = m_textures.emplace_back();
		texture.setTextureData(pData, (uint32_t)width, (uint32_t)height);

		return id;
	}

	void ResourceManager::loadObjects(FilePath path, std::vector<LoadedObject>& loadedObjects, float scale)
	{
		Assimp::Importer aiImporter;

		const aiScene* pAiScene = aiImporter.ReadFile(path.string(), aiProcess_ConvertToLeftHanded | aiProcess_Triangulate |
			aiProcess_OptimizeMeshes | aiProcess_FindInstances | aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace);
		
		OKAY_ASSERT(pAiScene);

		m_meshes.reserve(m_meshes.size() + pAiScene->mNumMeshes);
		uint32_t startMeshIdx = (uint32_t)m_meshes.size();

		MeshData meshData;
		for (uint32_t i = 0; i < pAiScene->mNumMeshes; i++)
		{
			convertMeshData(pAiScene->mMeshes[i], meshData, scale);

			m_meshes.emplace_back(meshData);

			meshData.verticies.clear();
			meshData.indicies.clear();
		}

		std::unordered_map<std::string, AssetID> texturePathToID;

		std::stack<aiNode*> aiNodeStack;
		aiNodeStack.emplace() = pAiScene->mRootNode;

		FilePath folderPath = path.parent_path();

		while (!aiNodeStack.empty())
		{
			aiNode* pAiNode = aiNodeStack.top();
			aiNodeStack.pop();

			glm::mat4 nodeTransform = assimpToGlmMat4(pAiNode->mTransformation);
			nodeTransform[3] *= scale;
			nodeTransform[3].w = 1.f;

			for (uint32_t i = 0; i < pAiNode->mNumMeshes; i++)
			{
				LoadedObject& objectData = loadedObjects.emplace_back();

				objectData.transformMatrix = nodeTransform;

				uint32_t aiMeshIdx = pAiNode->mMeshes[i];
				objectData.meshID = startMeshIdx + aiMeshIdx;

				aiMesh* pAiMesh = pAiScene->mMeshes[aiMeshIdx];
				aiMaterial* pAiMaterial = pAiScene->mMaterials[pAiMesh->mMaterialIndex];

				findOrLoadTexture(pAiMaterial, aiTextureType_DIFFUSE, texturePathToID, folderPath, this, objectData.diffuseTextureID);
				findOrLoadTexture(pAiMaterial, aiTextureType_DISPLACEMENT, texturePathToID, folderPath, this, objectData.normalMapID);
			}

			for (uint32_t i = 0; i < pAiNode->mNumChildren; i++)
			{
				aiNodeStack.emplace(pAiNode->mChildren[i]);
			}
		}
	}

	void ResourceManager::unloadCPUData()
	{
		for (Mesh& mesh : m_meshes)
		{
			mesh.clearData();
		}

		for (Texture& texture : m_textures)
		{
			stbi_image_free(texture.getTextureData());
			texture.setTextureData(nullptr, 0, 0);
		}
	}
}
