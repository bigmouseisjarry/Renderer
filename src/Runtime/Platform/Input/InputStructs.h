#pragma once


// 鼠标状态
struct MouseState
{
	float x = 0;                      // 当前鼠标位置
	float y = 0;                      // 当前鼠标位置
	float deltaX = 0.0f;            // 鼠标移动增量（每帧更新）
	float deltaY = 0.0f;            // 鼠标移动增量（每帧更新）
	float wheelDelta = 0.0f;        // 滚轮增量（正数上滚，负数下滚）
	bool wheelUpPressed = false;    // 滚轮上滚事件
	bool wheelDownPressed = false;  // 滚轮下滚事件
};