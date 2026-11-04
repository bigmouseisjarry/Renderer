#include "../../include/set2/set2.glsl"
#include "../../include/set0/set0.glsl"
#include "../../include/sky.glsl"
#include "../../include/coordinate.glsl"
#include "../../include/screen.glsl"
#include "../../include/ray_query.glsl"
#include "../../include/ddgi.glsl"
#include "../../include/sampler.glsl"
#include "../../include/pbs.glsl"
#include "../../include/gbuffer.glsl"
#include "../../include/light.glsl"
#include "../../include/reprojection.glsl"
#include "../../include/gizmo.glsl"
#include "../../include/surface_cache.glsl"
#define NUM_NEIGHBORS 1
// GI半分辨率：reservoir三缓冲从432MB降到108MB（6GB显存95%占用触发WDDM驱逐→被驱逐
// 的pass走PCIe慢20-40倍，即"帧率劣化"根因）
#define WIDTH_DOWNSAMPLE_RATE 2
#define HEIGHT_DOWNSAMPLE_RATE 2

#include "reservoir.glsl"

//////////////////////////////////////////////////////////////////////////



float EvaluatePHat(in HitSample hitSample)
{
	return RGBtoLuminance(hitSample.outRadiance);
}
