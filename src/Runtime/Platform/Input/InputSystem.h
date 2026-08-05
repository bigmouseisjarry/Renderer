#pragma once
#include "InputStructs.h"
#include <SDL3/SDL.h>
#include <array>

// TODO:目前未处理的有，水平滚轮、滚轮布尔标志被覆盖、无窗口 resize 事件处理等

class InputSystem
{
public:

    InputSystem() {};
    ~InputSystem() {};

    bool Tick();

    bool IsKeyDown(SDL_Scancode scancode)const;             // 按键一直按着
    bool IsKeyPressed(SDL_Scancode scancode)const;          // 按键刚刚按下
    bool IsKeyReleased(SDL_Scancode scancode)const;         // 按键刚刚抬起

    bool IsKeyDown(uint8_t mousecode)const;             // 按键一直按着
    bool IsKeyPressed(uint8_t mousecode)const;          // 按键刚刚按下
    bool IsKeyReleased(uint8_t mousecode)const;         // 按键刚刚抬起

    // 鼠标状态
    inline const MouseState& GetMouseState() const { return m_Mouse; };

private:
    void BeginFrame();
    void ProcessEvent(const SDL_Event& event);
    void EndFrame();

private:

    // 输入状态数组
    std::array<bool, SDL_SCANCODE_COUNT> m_KeyDown{};
    std::array<bool, SDL_SCANCODE_COUNT> m_PrevKeyDown{};
    std::array<bool, SDL_SCANCODE_COUNT> m_KeyPressed{};
    std::array<bool, SDL_SCANCODE_COUNT> m_KeyReleased{};

    std::array<bool, 8> m_MouseDown{};
    std::array<bool, 8> m_PrevMouseDown{};
    std::array<bool, 8> m_MousePressed{};
    std::array<bool, 8> m_MouseReleased{};

    bool quit = false;

    MouseState m_Mouse{};

};