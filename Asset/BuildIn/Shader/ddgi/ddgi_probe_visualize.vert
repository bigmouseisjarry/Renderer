
#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"
#include "ddgi_layout.glsl"

layout(push_constant) uniform DDGIVisualizeSetting {
    int volumeLightID;
    float probeScale;
    int visualizeMode;
    int vertexID;
} SETTING;

layout(location = 0) out vec4 OUT_POSITION;
layout(location = 1) out vec3 OUT_NORMAL;
layout(location = 2) out int OUT_ID;

void main() 
{
    int instance        = gl_InstanceIndex;
    uint indexOffset    = gl_VertexIndex;

    vec4 position = FetchVertexPos(SETTING.vertexID, indexOffset);
    vec3 normal   = FetchVertexNormal(SETTING.vertexID, indexOffset);



    VolumeLight volumeLight = LIGHTS.volumeLights[SETTING.volumeLightID];
    
    ivec3 gridCoord         = FetchProbeCoord(volumeLight.setting, instance);
    vec3 probePosition      = FetchProbeWorldPos(volumeLight.setting, gridCoord);
    vec4 worldPos           = vec4(position.xyz * SETTING.probeScale + probePosition, 1.0f);

    OUT_POSITION            = worldPos;
    OUT_ID                  = instance;
    OUT_NORMAL              = normal;

    gl_Position = CAMERA.viewProj * worldPos;
}








