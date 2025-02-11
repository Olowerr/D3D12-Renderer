#pragma once

#include "Engine/Okay.h"

#include "entt/entt.hpp"

#define ASSERT_ENTITY() OKAY_ASSERT(m_pRegistry); OKAY_ASSERT(m_pRegistry->valid(m_enttID))

namespace Okay
{
	class Entity
	{
	public:
		Entity() = default;

		Entity(entt::entity id, entt::registry& registry)
			:m_enttID(id), m_pRegistry(&registry)
		{
		}

		Entity(entt::registry& registry)
			:m_enttID(registry.create()), m_pRegistry(&registry)
		{
		}

		template<typename T, typename... Args>
		inline T& addComponent(Args&&... args)
		{
			ASSERT_ENTITY();

			return m_pRegistry->emplace_or_replace<T>(args...);
		}

		template<typename T>
		inline T& getComponent()
		{
			ASSERT_ENTITY();
			OKAY_ASSERT(hasComponent<T>());

			return m_pRegistry->get<T>(m_enttID);
		}

		template<typename T>
		inline const T& getComponent() const
		{
			ASSERT_ENTITY();
			OKAY_ASSERT(hasComponent<T>());

			return m_pRegistry->get<T>(m_enttID);
		}

		template<typename... T>
		inline bool hasComponents() const
		{
			ASSERT_ENTITY();

			return m_pRegistry->all_of<T...>(m_enttID);
		}

		inline bool isValid() const
		{
			return m_pRegistry ? m_pRegistry->valid(m_enttID) : false;
		}

	private:
		entt::entity m_enttID = entt::null;
		entt::registry* m_pRegistry = nullptr;

	};
}