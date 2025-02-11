#pragma once

#include "Window.h"
#include "Engine/Scene/Scene.h"

namespace Okay
{
	class Application
	{
	public:
		Application(std::string_view windowName, uint32_t windowWidth, uint32_t windowHeight);
		virtual ~Application();

		void run();

		virtual void onUpdate(float dt) = 0;

	protected:
		Okay::Scene m_scene;

	private:

		Okay::Window m_window;

	};
}
