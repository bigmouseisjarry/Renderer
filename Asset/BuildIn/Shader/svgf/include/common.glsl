#include "../../include/rand.glsl"
#include "../../include/set2/set2.glsl"
#include "../../include/set0/set0.glsl"
#include "../../include/sky.glsl"
#include "../../include/coordinate.glsl"
#include "../../include/screen.glsl"
#include "../../include/ray_query.glsl"
#include "../../include/ddgi.glsl"
#include "../../include/brdf.glsl"
#include "../../include/samples.glsl"
#include "../../include/noise.glsl"
#include "../../include/light.glsl"
#include "../../include/reprojection.glsl"
#include "../../include/gizmo.glsl"
#include "../../include/surface_cache.glsl"

#ifndef SVGF_COMMON_GLSL
#define SVGF_COMMON_GLSL


const float epsilon = 0.00001;

//const float h[5] = float[5](1.0 / 16.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0);
// const float h[25] = float[25](
//     1.0 / 256.0, 1.0 / 64.0, 3.0 / 128.0, 1.0 / 64.0, 1.0 / 256.0,
//     1.0 / 64.0,  1.0 / 16.0, 3.0 / 32.0,  1.0 / 16.0, 1.0 / 64.0,
//     3.0 / 128.0, 3.0 / 32.0, 9.0 / 64.0,  3.0 / 32.0, 3.0 / 128.0,
//     1.0 / 64.0,  1.0 / 16.0, 3.0 / 32.0,  1.0 / 16.0, 1.0 / 64.0,
//     1.0 / 256.0, 1.0 / 64.0, 3.0 / 128.0, 1.0 / 64.0, 1.0 / 256.0
// );

const float gaussKernel3x3[9] = float[9](
    1, 2, 1, 
    2, 4, 2, 
    1, 2, 1
);  // 16

const float gaussKernel5x5[25] = float[25](
    1, 4,  7,  4,  1,
    4, 16, 26, 16, 4,
    7, 26, 41, 26, 7, 
    4, 16, 26, 16, 4,    
    1, 4,  7,  4,  1
);  // 273

// const float gaussKernel7x7[49] = float[49](
//     0, 0,  1,  2,   1,  0,  0,
//     0, 3,  13, 22,  13, 3,  0,
//     1, 13, 59, 97,  59, 13, 1,
//     2, 22, 97, 159, 97, 22, 2,
//     1, 13, 59, 97,  59, 13, 1,
//     0, 3,  13, 22,  13, 3,  0,
//     0, 0,  1,  2,   1,  0,  0
// );  // 1003

float GetPlaneDistanceWeight(vec3 centerWorldPos, vec3 centerNormal, vec3 sampleWorldPos, float sigmaP)  
{
    float distanceToCenterPointPlane = abs(dot(sampleWorldPos - centerWorldPos, centerNormal));     // 原论文是深度项，ReLAX使用的是点面距离

    return distanceToCenterPointPlane < sigmaP ? 1.0 : 0.0;
}

float GetLuminanceWeight(float centerLuminance, float sampleLuminance, float variance, float sigmaL)
{
    return min(exp(-abs(centerLuminance - sampleLuminance) / (sigmaL * sqrt(variance) + epsilon)), 1.0f);  // 亮度项
}

float GetNormalWeight(vec3 centerNormal, vec3 sampleNormal, float sigmaN)
{
    return pow(max(0.0f, dot(centerNormal, sampleNormal)), sigmaN);
}
#endif
