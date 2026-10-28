#ifndef SET0_GLSL
#define SET0_GLSL
#include "defines.glsl"
#include "../math.glsl"
#include "../intersection.glsl"
#include "../types.glsl"
layout(set = 0, binding = SET0_BINDING_GLOBAL_SETTING) buffer global_setting {
    
    uint skyboxMaterialID;
    uint clusterInspectMode;    
    uint totalTicks;
    float totalTickTime;
    float minFrameTime;
    float rayTracingOffset;

    GlobalIconInfo icons;

} GLOBAL_SETTING;

layout(set = 0, binding = SET0_BINDING_CAMERA) uniform camera {

    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 prevView;
    mat4 prevProj;
    mat4 prevViewProj;
    mat4 invView;
    mat4 invProj;

    vec4 pos;  

    vec3 front;
    vec3 up;
    vec3 right;
    float _padding;

    float near;   
    float far;
    float fov;
    float aspect;       

    Frustum frustum;
            
} CAMERA;
#endif
