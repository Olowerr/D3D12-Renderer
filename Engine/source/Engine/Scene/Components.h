#pragma once

#include "Engine/Okay.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include "glm/gtx/quaternion.hpp"
#include "glm/gtx/matrix_decompose.hpp"

namespace Okay
{
	struct Transform
	{
		glm::vec3 position = glm::vec3(0.f);
		glm::vec3 rotation = glm::vec3(0.f);
		glm::vec3 scale = glm::vec3(1.f);

		inline glm::mat4 getMatrix() const
		{
			return glm::translate(glm::mat4(1.f), position) *
				glm::toMat4(glm::quat(glm::radians(rotation))) *
				glm::scale(glm::mat4(1.f), scale);
		}

		inline glm::mat4 getViewMatrix() const
		{
			return glm::lookAtLH(position, position + forwardVec(), upVec());
		}

		inline glm::vec3 forwardVec() const { return glm::normalize(getMatrix()[2]); }
		inline glm::vec3 rightVec() const { return glm::normalize(getMatrix()[0]); }
		inline glm::vec3 upVec() const { return glm::normalize(getMatrix()[1]); }

		inline void setFromMatrix(const glm::mat4& matrix)
		{
			glm::quat rotationQuat = {};
			glm::vec3 skew = {};
			glm::vec4 perspective = {};

			glm::decompose(matrix, scale, rotationQuat, position, skew, perspective);

			rotation = glm::degrees(glm::eulerAngles(rotationQuat));
		}
	};

	struct Camera
	{
		float nearZ = 1.f;
		float farZ = 5000.f;
		float fov = 90.f;

		inline glm::mat4 getProjectionMatrix(float width, float height) const
		{
			return glm::perspectiveFovLH_ZO(glm::radians(fov), width, height, nearZ, farZ);
		}
	};

	struct MeshRenderer
	{
		AssetID meshID = INVALID_ASSET_ID;
		AssetID diffuseTextureID = INVALID_ASSET_ID;
		AssetID normalMapID = INVALID_ASSET_ID;
	};

	struct PointLight
	{
		glm::vec3 colour = glm::vec3(1.f);
		float intensity = 1.f;
		glm::vec2 attenuation = glm::vec2(0.f, 1.f);
	};

	struct DirectionalLight
	{
		glm::vec3 colour = glm::vec3(1.f);
		float intensity = 1.f;
	};

	struct SpotLight
	{
		glm::vec3 colour = glm::vec3(1.f);
		float intensity = 1.f;
		glm::vec2 attenuation = glm::vec2(0.f, 1.f);
		float spreadAngle = 90.f;
	};
}
