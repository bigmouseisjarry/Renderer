#version 460
#extension GL_GOOGLE_include_directive : enable

#include "include/common.glsl"

layout(location = 0) in flat uint IN_TEXTURE_ID;
layout(location = 1) in vec2 IN_UV;
layout(location = 2) in vec4 VIS_IN_COLOR;

layout(location = 0) out vec4 VIS_OUT_COLOR;

void main()
{
    VIS_OUT_COLOR = VIS_IN_COLOR;
    if(IN_TEXTURE_ID != 0) VIS_OUT_COLOR *= pow(FetchTex2D(IN_TEXTURE_ID, IN_UV, 0), vec4(1.0/2.2));         

    if(VIS_OUT_COLOR.w < 0.00001f) discard;
}

