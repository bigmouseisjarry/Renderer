#version 460
#extension GL_GOOGLE_include_directive : enable

#include "include/common.glsl"
#include "../include/iridescence.glsl"

layout(location = 0) in vec4 IN_POSITION;
layout(location = 1) in vec4 IN_PREV_POSITION;
layout(location = 2) in vec3 VIS_IN_COLOR;
layout(location = 3) in vec2 IN_TEXCOORD;
layout(location = 4) in vec3 IN_NORMAL;
layout(location = 5) in vec4 IN_TANGENT;
layout(location = 6) in flat uint IN_ID;

layout (location = 0) out vec4 OUT_GBUFFER_DIFFUSE;
layout (location = 1) out vec4 OUT_GBUFFER_NORMAL;
layout (location = 2) out vec4 OUT_GBUFFER_EMISSION;
layout (location = 3) out vec2 OUT_GBUFFER_VELOCITY;
layout (location = 4) out uint OUT_GBUFFER_OBJECT_ID;
layout (location = 5) out vec4 OUT_GBUFFER_GEOMETRIC_NORMAL_AO;
layout (location = 6) out vec4 OUT_GBUFFER_SPECULAR_F0;
layout (location = 7) out float OUT_GBUFFER_IOR;

void main()
{
    Material material   = FetchMaterial(IN_ID);
    vec4 color          = vec4(VIS_IN_COLOR, 1.0f);
    vec4 albedo         = FetchAlbedo(material, IN_TEXCOORD);
    vec3 emission       = FetchEmission(material);
    vec3 normal         = FetchNormal(material, IN_TEXCOORD, IN_NORMAL, IN_TANGENT);
    float roughness     = FetchRoughness(material, IN_TEXCOORD);
    float metallic      = FetchMetallic(material, IN_TEXCOORD);

    if(material.useVertexColor != 0)
        albedo.xyz *= color.xyz;

    if(material.alphaClip >= albedo.a)
        discard;    // 透明度测试

    //emission /= FetchEmissionIntencity(material);   // TODO 自发光直射进屏幕的结果很奇怪？

    vec3 diffusePremultiplied   = albedo.xyz * (1.0f - metallic);   
    vec3 F0                     = mix(vec3(0.04f), albedo.xyz, metallic);

    if(material.iridescence != 0)
    {
        vec3 V = normalize(CAMERA.pos.xyz - IN_POSITION.xyz);
        float cosTheta1 = clamp(dot(normal, V), 0.0, 1.0);
        float cosTheta2 = sqrt(max(0.0, 1.0 - (1.0 - cosTheta1 * cosTheta1) / max(1e-7, material.ior * material.ior)));    
        F0 = IridescenceFresnel(cosTheta1, cosTheta2, material.ior, material.index2, metallic, material.dinc);
    }

    if(GLOBAL_SETTING.clusterInspectMode == 0) OUT_GBUFFER_DIFFUSE = vec4(diffusePremultiplied, 1.0f - albedo.a);
    else                                       OUT_GBUFFER_DIFFUSE = color;

    OUT_GBUFFER_NORMAL                 = vec4(EncodeGBufferNormal(normal), material.iridescence != 0 ? 1.0f : 0.0f);   
    OUT_GBUFFER_EMISSION               = EncodeEmissionRGBM(emission);        
    OUT_GBUFFER_VELOCITY               = CalculateVelocity(IN_POSITION, IN_PREV_POSITION);
    OUT_GBUFFER_OBJECT_ID              = IN_ID;
    OUT_GBUFFER_GEOMETRIC_NORMAL_AO    = vec4(EncodeGBufferNormal(normalize(IN_NORMAL)), FetchAO(material, IN_TEXCOORD));   
    OUT_GBUFFER_SPECULAR_F0            = vec4(F0, roughness);    
    OUT_GBUFFER_IOR                    = (material.ior - 1.0f) * 0.5f;   
}
