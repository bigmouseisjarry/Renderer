#include "InputSystem.h"
#include <spdlog/spdlog.h>
#include <imgui_impl_sdl3.h>

bool InputSystem::Tick()
{
	static SDL_Event event;

    BeginFrame();
	// 处理此帧的所有事件
    while(SDL_PollEvent(&event))
    {
        ImGui_ImplSDL3_ProcessEvent(&event);
        ProcessEvent(event);
    }
    EndFrame();
    return quit;    
}

bool InputSystem::IsKeyDown(SDL_Scancode scancode) const
{
    if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT)
        return false;
    return m_KeyDown[scancode];
}

bool InputSystem::IsKeyPressed(SDL_Scancode scancode) const
{
    if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT)
        return false;
    return m_KeyPressed[scancode];
}

bool InputSystem::IsKeyReleased(SDL_Scancode scancode) const
{
    if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT)
        return false;
    return m_KeyReleased[scancode];
}

bool InputSystem::IsKeyDown(uint8_t mousecode) const
{
    if (mousecode < 1 || mousecode > 8)
        return false;
    return m_MouseDown[mousecode - 1];
}

bool InputSystem::IsKeyPressed(uint8_t mousecode) const
{
    if (mousecode < 1 || mousecode > 8)
        return false;
    return m_MousePressed[mousecode - 1];
}

bool InputSystem::IsKeyReleased(uint8_t mousecode) const
{
    if (mousecode < 1 || mousecode > 8)
        return false;
    return m_MouseReleased[mousecode - 1];
}

void InputSystem::BeginFrame()
{
    // 重置 Pressed 和 Released
    m_KeyPressed.fill(false);
    m_KeyReleased.fill(false);
    m_MousePressed.fill(false);
    m_MouseReleased.fill(false);

    // 保存上一帧的 Down 状态
    m_PrevKeyDown = m_KeyDown;
    m_PrevMouseDown = m_MouseDown;

    // 重置鼠标增量
    m_Mouse.deltaX = 0.0f;
    m_Mouse.deltaY = 0.0f;

    m_Mouse.wheelDelta = 0.0f;
    m_Mouse.wheelUpPressed = false;
    m_Mouse.wheelDownPressed = false;
}

void InputSystem::ProcessEvent(const SDL_Event & event)
{
    // 退出事件
    if (event.type == SDL_EVENT_QUIT)
    {
        quit = true;
    }
    else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
		// TODO：当存在多个窗口时，应该具体判断哪个窗口关闭
        // event.window.windowID == SDL_GetWindowID(window)
        quit = true;
    }
    // 键盘按键
    else if (event.type == SDL_EVENT_KEY_DOWN)
    {
        SDL_Scancode sc = event.key.scancode;
        if (sc >= 0 && sc < SDL_SCANCODE_COUNT)
        {
            m_KeyDown[sc] = true; // 只要按下就设为 true，不管重复
        }
    }
    else if (event.type == SDL_EVENT_KEY_UP)
    {
        SDL_Scancode sc = event.key.scancode;
        if (sc >= 0 && sc < SDL_SCANCODE_COUNT)
        {
            m_KeyDown[sc] = false; // 只要抬起就设为 false，不管重复
        }
    }
    // 鼠标按键
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        Uint8 btn = event.button.button;
        if (btn >= 1 && btn <= 8)
        {
            m_MouseDown[btn - 1] = true; // SDL_BUTTON_LEFT 是 1，数组从 0 开始
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        Uint8 btn = event.button.button;
        if (btn >= 1 && btn <= 8)
        {
            m_MouseDown[btn - 1] = false; // SDL_BUTTON_LEFT 是 1，数组从 0 开始
        }
    }
    // 鼠标移动
    else if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        // 更新位置
        m_Mouse.x = event.motion.x;
        m_Mouse.y = event.motion.y;

        // 计算增量
        m_Mouse.deltaX += event.motion.xrel;
        m_Mouse.deltaY += event.motion.yrel;

    }
    // 鼠标滚轮
    else if (event.type == SDL_EVENT_MOUSE_WHEEL)
    {
        int flip = (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -1 : 1;
        m_Mouse.wheelDelta += (flip * event.wheel.y);

        m_Mouse.wheelDownPressed = (m_Mouse.wheelDelta < 0.0f);
        m_Mouse.wheelUpPressed = (m_Mouse.wheelDelta > 0.0f);
    }

	if (m_Mouse.deltaX != 0 || m_Mouse.deltaY != 0)
	{
		SPDLOG_INFO("Mouse motion: deltaX = {}, deltaY = {}", m_Mouse.deltaX, m_Mouse.deltaY);
	}
}

void InputSystem::EndFrame()
{
    // Pressed = 当前 Down 且 上一帧 !Down（刚按下）
    // Released = 当前 !Down 且 上一帧 Down（刚松开）

    // 键盘
    for (int i = 0; i < SDL_SCANCODE_COUNT; ++i)
    {
        m_KeyPressed[i] = m_KeyDown[i] && !m_PrevKeyDown[i];
        m_KeyReleased[i] = !m_KeyDown[i] && m_PrevKeyDown[i];
    }

    // 鼠标按钮
    for (int i = 0; i < 8; ++i)
    {
        m_MousePressed[i] = m_MouseDown[i] && !m_PrevMouseDown[i];
        m_MouseReleased[i] = !m_MouseDown[i] && m_PrevMouseDown[i];
    }
}
