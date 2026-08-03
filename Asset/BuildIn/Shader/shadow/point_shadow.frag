#version 460
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"

layout(push_constant) uniform point_light_setting {
    uint index;
    uint face;
}POINT_LIGHT_SETTING;

layout(location = 0) in vec3 IN_POS;
layout(location = 1) in vec2 IN_TEXCOORD;
layout(location = 2) in flat uint IN_ID;
layout(location = 0) out vec2 OUT_COLOR;

void main() 
{	
    Material material = FetchMaterial(IN_ID);
    vec4 diffuse = FetchDiffuse(material, IN_TEXCOORD);
    if(material.alphaClip >= diffuse.a) 
        discard;    // 透明度测试

    PointLight light = LIGHTS.pointLights[POINT_LIGHT_SETTING.index];

    float depth = length(IN_POS.xyz - light.pos) / light.far;  
    //gl_FragDepth = depth;

#ifdef SHADOW_QUALITY_LOW	// 直接用深度

    OUT_COLOR = vec2(depth, 1.0); //距离作为深度

#else	// 使用VSM
        // EVSM很没必要
    OUT_COLOR.r = exp(light.c1 * depth);
    OUT_COLOR.g = OUT_COLOR.r * OUT_COLOR.r;
    // OUT_COLOR.b = exp(-light.c2 * depth);
    // OUT_COLOR.a = OUT_COLOR.b * OUT_COLOR.b;

#endif
}