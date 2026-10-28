#version 460
#extension GL_GOOGLE_include_directive : enable

#include "include/common.glsl"

layout(location = 0) in vec4 VIS_IN_COLOR;
layout(location = 0) out vec4 VIS_OUT_COLOR;

void main()
{
    VIS_OUT_COLOR = VIS_IN_COLOR;
}

