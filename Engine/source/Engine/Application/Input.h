#pragma once
#include "KeyCodes.h"

#include "glm/vec2.hpp"

#include <unordered_map>

namespace Okay
{
	class Window;

	class Input
	{
	public:
		friend class Window;

		static bool isKeyPressed(Key keyCode);
		static bool isKeyDown(Key keyCode);
		static bool isKeyReleased(Key keyCode);

		static bool isMouseButtonPressed(MouseButton buttonCode);
		static bool isMouseButtonDown(MouseButton buttonCode);
		static bool isMouseButtonReleased(MouseButton buttonCode);

		static void setMouseMode(MouseMode mode);
		static MouseMode getMouseMode();

		static glm::vec2 getMouseDelta();

	private:
		static void preInputHandling();

		static void setKeyDown(Key keyCode);
		static void setKeyUp(Key keyCode);

		static void setMouseButtonDown(MouseButton buttonCode);
		static void setMouseButtonUp(MouseButton buttonCode);

		static void setMousePosition(glm::vec2 position);

	private:
		static Window* s_pWindow;

		static std::unordered_map<Key, bool> s_keysLastFrame;
		static std::unordered_map<Key, bool> s_keysCurrentFrame;

		static std::unordered_map<MouseButton, bool> s_mouseButtonsLastFrame;
		static std::unordered_map<MouseButton, bool> s_mouseButtonsCurrentFrame;

		static glm::vec2 s_mousePosLastFrame;
		static glm::vec2 s_mousePosCurrentFrame;

	};
}
