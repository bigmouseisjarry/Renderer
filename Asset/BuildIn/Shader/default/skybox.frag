#version 460
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"

layout(location = 0) in vec3 IN_CUBE_SAMPLE_VECTOR;
layout(location = 1) in flat uint IN_ID;

layout (location = 0) out vec4 OUT_COLOR;

void main()
{
    OUT_COLOR = vec4(FetchSkyLight(IN_CUBE_SAMPLE_VECTOR), 1.0f);
    //OUT_COLOR = vec4(normalize(IN_CUBE_SAMPLE_VECTOR), 1.0);
}

