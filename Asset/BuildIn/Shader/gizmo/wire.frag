#version 460
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"

layout(location = 0) in vec4 IN_COLOR;
layout (location = 0) out vec4 OUT_COLOR;

void main()
{
    OUT_COLOR = IN_COLOR;
}

