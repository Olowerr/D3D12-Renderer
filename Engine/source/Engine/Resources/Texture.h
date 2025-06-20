#pragma once

#include "Engine/Okay.h"

#include "stb/stb_image.h"

namespace Okay
{
	class Texture
	{
	public:
		Texture() = default;
		virtual ~Texture() = default;

		inline uint32_t getWidth() const;
		inline uint32_t getHeight() const;

		inline void setTextureData(uint8_t* pData, uint32_t width, uint32_t height);
		inline uint8_t* getTextureData() const;

	private:
		uint8_t* m_pTextureData = nullptr;

		uint32_t m_width = INVALID_UINT32;
		uint32_t m_height = INVALID_UINT32;

	};

	inline uint32_t Texture::getWidth() const
	{
		return m_width;
	}
	
	inline uint32_t Texture::getHeight() const
	{
		return m_height;
	}
	
	inline void Texture::setTextureData(uint8_t* pData, uint32_t width, uint32_t height)
	{
		m_pTextureData = pData;
		m_width = width;
		m_height = height;
	}

	inline uint8_t* Texture::getTextureData() const
	{
		return m_pTextureData;
	}
}
