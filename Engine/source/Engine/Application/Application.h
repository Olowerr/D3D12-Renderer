#pragma once

#include "Window.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Graphics/Renderer.h"
#include "Time.h"

namespace Okay
{
	class Application
	{
	public:
		Application(std::string_view windowName, uint32_t windowWidth, uint32_t windowHeight);
		virtual ~Application();

		void run();

		// onStart, onEnd
		virtual void onUpdate(TimeStep dt) = 0;

	protected:
		Scene m_scene;

	private:
		Window m_window;
		Renderer m_renderer;

	};
}
