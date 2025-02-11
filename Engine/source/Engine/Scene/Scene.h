#pragma once

#include "Entity.h"

namespace Okay
{
	class Scene
	{
	public:
		Scene() = default;

		inline Entity createEntity()
		{
			Entity entity(m_registry);

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

	private:
		entt::registry m_registry;

	};
}
