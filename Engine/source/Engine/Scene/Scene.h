#pragma once

#include "Entity.h"
#include "Components.h"

namespace Okay
{
	class Scene
	{
	public:
		Scene() = default;

		inline Entity createEntity()
		{
			Entity entity(m_registry);
			entity.addComponent<Transform>();

			return entity;
		}

		inline entt::registry& getRegistry()
		{
			return m_registry;
		}
		
		inline const entt::registry& getRegistry() const
		{
			return m_registry;
		}

		inline void setActiveCamera(Entity camEntity)
		{
			OKAY_ASSERT(camEntity.isValid());
			OKAY_ASSERT(camEntity.hasComponents<Camera>());

			m_activeCamera = camEntity;
		}

		inline const Entity getActiveCamera() const
		{
			OKAY_ASSERT(m_activeCamera.isValid());
			OKAY_ASSERT(m_activeCamera.hasComponents<Camera>());

			return m_activeCamera;
		}

	private:
		entt::registry m_registry;
		Entity m_activeCamera = Entity();

	};
}
