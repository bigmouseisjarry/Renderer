#version 460
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"

layout(location = 0) in vec4 IN_POSITION;
layout(location = 1) in vec4 IN_PREV_POSITION;
layout(location = 2) in vec3 IN_COLOR;
layout(location = 3) in vec2 IN_TEXCOORD;
layout(location = 4) in vec3 IN_NORMAL;
layout(location = 5) in vec4 IN_TANGENT;
layout(location = 6) in flat uint IN_ID;

layout (location = 0) out vec4 G_BUFFER_DIFFUSE_METALLIC;
layout (location = 1) out vec4 G_BUFFER_NORMAL_ROUGHNESS;
layout (location = 2) out vec4 G_BUFFER_EMISSION;
layout (location = 3) out vec2 G_BUFFER_VELOCITY;
layout (location = 4) out uint G_BUFFER_OBJECT_ID;

void main() 
{
    Material material   = FetchMaterial(IN_ID);
    vec4 color          = vec4(IN_COLOR, 1.0f);
    vec4 diffuse        = FetchDiffuse(material, IN_TEXCOORD);
    vec3 emission       = FetchEmission(material);
    vec3 normal         = FetchNormal(material, IN_TEXCOORD, IN_NORMAL, IN_TANGENT);
    float roughness     = FetchRoughness(material, IN_TEXCOORD);
    float metallic      = FetchMetallic(material, IN_TEXCOORD);

    if(material.useVertexColor != 0)
        diffuse.xyz *= color.xyz;

    if(material.alphaClip >= diffuse.a) 
        discard;    // 透明度测试

    //emission /= FetchEmissionIntencity(material);   // TODO 自发光直射进屏幕的结果很奇怪？

    if(GLOBAL_SETTING.clusterInspectMode == 0) G_BUFFER_DIFFUSE_METALLIC = vec4(diffuse.xyz, metallic);
    else                                       G_BUFFER_DIFFUSE_METALLIC = color;
    
    G_BUFFER_NORMAL_ROUGHNESS    = vec4(normal, roughness);
    G_BUFFER_EMISSION           = vec4(emission, 0.0f);
    G_BUFFER_VELOCITY           = CalculateVelocity(IN_POSITION, IN_PREV_POSITION);
    G_BUFFER_OBJECT_ID          = IN_ID;
}
