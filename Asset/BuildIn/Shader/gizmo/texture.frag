#version 460
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"

layout(location = 0) in flat uint IN_TEXTURE_ID;
layout(location = 1) in vec2 IN_UV;
layout(location = 2) in vec4 IN_COLOR;
layout (location = 0) out vec4 OUT_COLOR;

void main()
{
    OUT_COLOR = IN_COLOR;
    if(IN_TEXTURE_ID != 0) OUT_COLOR *= pow(FetchTex2D(IN_TEXTURE_ID, IN_UV, 0), vec4(1.0/2.2));         

    if(OUT_COLOR.w < 0.00001f) discard;
}

