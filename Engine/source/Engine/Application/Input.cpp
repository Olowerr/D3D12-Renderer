#include "Input.h"
#include "Window.h"

namespace Okay
{
	std::unordered_map<Key, bool> Input::s_keysLastFrame;
	std::unordered_map<Key, bool> Input::s_keysCurrentFrame;

	std::unordered_map<MouseButton, bool> Input::s_mouseButtonsLastFrame;
	std::unordered_map<MouseButton, bool> Input::s_mouseButtonsCurrentFrame;

	glm::vec2 Input::s_mousePosLastFrame;
	glm::vec2 Input::s_mousePosCurrentFrame;

	Window* Input::s_pWindow = nullptr;


	bool Input::isKeyPressed(Key keyCode)
	{
		return !s_keysLastFrame[keyCode] && s_keysCurrentFrame[keyCode];
	}

	bool Input::isKeyDown(Key keyCode)
	{
		return s_keysCurrentFrame[keyCode];
	}

	bool Input::isKeyReleased(Key keyCode)
	{
		return s_keysLastFrame[keyCode] && !s_keysCurrentFrame[keyCode];
	}

	bool Input::isMouseButtonPressed(MouseButton buttonCode)
	{
		return !s_mouseButtonsLastFrame[buttonCode] && s_mouseButtonsCurrentFrame[buttonCode];
	}

	bool Input::isMouseButtonDown(MouseButton buttonCode)
	{
		return s_mouseButtonsCurrentFrame[buttonCode];
	}

	bool Input::isMouseButtonReleased(MouseButton buttonCode)
	{
		return s_mouseButtonsLastFrame[buttonCode] && !s_mouseButtonsCurrentFrame[buttonCode];
	}

	void Input::setMouseMode(MouseMode mode)
	{
		Input::s_pWindow->setInputMode(mode);
	}

	MouseMode Input::getMouseMode()
	{
		return Input::s_pWindow->getInputMode();
	}

	glm::vec2 Input::getMouseDelta()
	{
		return s_mousePosCurrentFrame - s_mousePosLastFrame;
	}

	void Input::preInputHandling()
	{
		s_keysLastFrame = s_keysCurrentFrame;
		s_mouseButtonsLastFrame = s_mouseButtonsCurrentFrame;

		s_mousePosLastFrame = s_mousePosCurrentFrame;
	}

	void Input::setKeyDown(Key keyCode)
	{
		s_keysCurrentFrame[keyCode] = true;
	}

	void Input::setKeyUp(Key keyCode)
	{
		s_keysCurrentFrame[keyCode] = false;
	}

	void Input::setMouseButtonDown(MouseButton buttonCode)
	{
		s_mouseButtonsCurrentFrame[buttonCode] = true;
	}

	void Input::setMouseButtonUp(MouseButton buttonCode)
	{
		s_mouseButtonsCurrentFrame[buttonCode] = false;
	}

	void Input::setMousePosition(glm::vec2 position)
	{
		s_mousePosCurrentFrame = position;
	}
}
