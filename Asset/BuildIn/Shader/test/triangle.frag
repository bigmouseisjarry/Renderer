#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "include/common.glsl"

layout (location = 0) in vec2 IN_UV;
layout (location = 0) out vec4 OUT_TRIANGLE_COLOR;

void main() 
{  
    OUT_TRIANGLE_COLOR = vec4(IN_UV, 0.0f, 1.0f);
}