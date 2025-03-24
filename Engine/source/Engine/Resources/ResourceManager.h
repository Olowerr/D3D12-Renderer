#pragma once

#include "Mesh.h"
#include "Texture.h"

#include <filesystem>
#include <vector>

namespace Okay
{
	class ResourceManager
	{
	public:
		ResourceManager() = default;
		~ResourceManager() = default;

		AssetID loadMesh(FilePath path);
		AssetID loadTexture(FilePath path);

		void unloadCPUData();

		template<typename Asset>
		inline Asset& getAsset(AssetID id);

		template<typename Asset>
		inline const Asset& getAsset(AssetID id) const;

		template<typename Asset>
		inline uint32_t getCount() const;

		template<typename Asset>
		inline const std::vector<Asset>& getAll() const;

	private:
		template<typename Asset>
		inline std::vector<Asset>& getAssets();

		template<typename Asset>
		inline const std::vector<Asset>& getAssetsConst() const;

		void importMeshData(FilePath path, MeshData& outData);

	private:
		std::vector<Mesh> m_meshes;
		std::vector<Texture> m_textures;

	};


	#define STATIC_ASSERT_ASSET_TYPE()		\
		static_assert(						\
			std::is_same<Asset, Mesh>()	||	\
			std::is_same<Asset, Texture>(),	\
			"Invalid Asset type")

	// Public:
	template<typename Asset>
	inline Asset& ResourceManager::getAsset(AssetID id)
	{
		STATIC_ASSERT_ASSET_TYPE();
		std::vector<Asset>& assets = getAssets<Asset>();

		sizeof(Mesh);

		OKAY_ASSERT((uint32_t)id < (uint32_t)assets.size());
		return assets[id];
	}

	template<typename Asset>
	inline const Asset& ResourceManager::getAsset(AssetID id) const
	{
		STATIC_ASSERT_ASSET_TYPE();
		const std::vector<Asset>& assets = getAssetsConst<Asset>();

		OKAY_ASSERT((uint32_t)id < (uint32_t)assets.size());
		return assets[id];
	}

	template<typename Asset>
	inline uint32_t ResourceManager::getCount() const
	{
		STATIC_ASSERT_ASSET_TYPE();
		return (uint32_t)getAssetsConst<Asset>().size();
	}

	template<typename Asset>
	inline const std::vector<Asset>& ResourceManager::getAll() const
	{
		STATIC_ASSERT_ASSET_TYPE();
		return getAssetsConst<Asset>();
	}


	// Private
	template<typename Asset>
	inline std::vector<Asset>& ResourceManager::getAssets()
	{
		STATIC_ASSERT_ASSET_TYPE();

		if constexpr (std::is_same<Asset, Mesh>())
		{
			return m_meshes;
		}

		if constexpr (std::is_same<Asset, Texture>())
		{
			return m_textures;
		}
	}

	template<typename Asset>
	inline const std::vector<Asset>& ResourceManager::getAssetsConst() const
	{
		STATIC_ASSERT_ASSET_TYPE();
		return const_cast<ResourceManager*>(this)->getAssets<Asset>();
	}
}
