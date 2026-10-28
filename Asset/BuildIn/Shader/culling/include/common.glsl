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

#define DEPTH_PASS_INDEX 0
#define POINT_SHADOW_PASS_INDEX 1
#define DIRECTIONAL_SHADOW_PASS_INDEX 2
#define G_BUFFER_PASS_INDEX 3
#define FORWARD_PASS_INDEX 4
#define TRANSPARENT_PASS_INDEX 5
#define G_BUFFER_THIN_PASS_INDEX 6

bool CheckLOD(BoundingSphere sphere, float error, float lodErrorRate, float lodErrorOffset)
{
    float dist = max(length(sphere.centerRadius.xyz/*于摄像机的距离*/) - sphere.centerRadius.w, 0.0f) / sphere.centerRadius.w;	// 库里的error是个与物体大小无关的百分比度量

    return lodErrorRate * dist + lodErrorOffset >= error;
}
