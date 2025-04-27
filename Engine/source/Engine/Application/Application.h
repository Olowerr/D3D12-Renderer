#pragma once

#include "Window.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Graphics/Renderer.h"
#include "Engine/Resources/ResourceManager.h"

#include "Input.h"
#include "Time.h"

namespace Okay
{
	class Application
	{
	public:
		Application(std::string_view windowTitle, uint32_t windowWidth, uint32_t windowHeight);
		virtual ~Application();

		void run();

	protected:
		// onStart, onEnd
		virtual void onUpdate(TimeStep dt) = 0;

		void createEntitesFromFile(FilePath path, float scale);

	protected:
		Scene m_scene;
		ResourceManager m_resourceManager;
		Window m_window;

	private:
		Renderer m_renderer;

	};
}
