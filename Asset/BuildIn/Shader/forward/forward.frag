#version 460
#extension GL_GOOGLE_include_directive : enable

#include "include/common.glsl"

layout(location = 0) in vec4 IN_POSITION;
layout(location = 1) in vec4 IN_PREV_POSITION;
layout(location = 2) in vec3 VIS_IN_COLOR;
layout(location = 3) in vec2 IN_TEXCOORD;
layout(location = 4) in vec3 IN_NORMAL;
layout(location = 5) in vec4 IN_TANGENT;
layout(location = 6) in flat uint IN_ID;

layout (location = 0) out vec4 VIS_OUT_COLOR;

void main()
{
    // 采集（GetSurfaceInfo，bakeIridescence=true——透明直通路径与thin同族，对位HRP的Transparent_frag inline着色版；
    // 采集就绪等PBS直通接入，当前输出为stub预览：预乘diffuse）
    SurfaceInfo surface = GetSurfaceInfo(IN_ID, IN_TEXCOORD, IN_POSITION.xyz, IN_NORMAL, IN_TANGENT, VIS_IN_COLOR, true);

    if(FetchMaterial(IN_ID).alphaClip >= 1.0f - surface.transparent)
        discard;    // 透明度测试

    if(GLOBAL_SETTING.clusterInspectMode == 0) VIS_OUT_COLOR = vec4(surface.diffuse, 1.0f);
    else                                       VIS_OUT_COLOR = vec4(VIS_IN_COLOR, 1.0f);
}