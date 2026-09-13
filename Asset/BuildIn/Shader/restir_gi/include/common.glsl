#define NUM_NEIGHBORS 1
// GI半分辨率：reservoir三缓冲从432MB降到108MB（6GB显存95%占用触发WDDM驱逐→被驱逐
// 的pass走PCIe慢20-40倍，即"帧率劣化"根因）
#define WIDTH_DOWNSAMPLE_RATE 2
#define HEIGHT_DOWNSAMPLE_RATE 2

#include "reservoir.glsl"

struct RestirGISetting
{
	int initialSampleCount;

	int temporalSampleCountMultiplier;
	float temporalPosThreshold;

	float spatialPosThreshold;
	float spatialNormalThreshold;
	float spatialRadius;

	float surfaceCacheFactor;

	uint combineMode;
    uint visibilityReuse;
    uint temporalReuse;
    uint spatialReuse;
	uint unbias;
	uint enableSkybox;                   
	uint showRadiance;             
};

//////////////////////////////////////////////////////////////////////////

layout (set = 1, binding = 0) buffer setting
{
	RestirGISetting RESTIR_GI_SETTING;
};

layout (set = 1, binding = 1) buffer Reservoirs 
{
	GIReservoir RESERVOIRS[];                         // 本帧新生成的Reservoir(以及时域合并的结果)
};

layout (set = 1, binding = 2) buffer PrevFrameReservoirs 
{
	GIReservoir PREV_RESERVOIRS[];                    // 前一帧的Reservoir结果
};

layout (set = 1, binding = 3) buffer ResultReservoirs 
{
	GIReservoir RESULT_RESERVOIRS[];                  // 本帧Reservoir空域合并的结果
};

layout(set = 2, binding = 0)               uniform texture2D G_BUFFER_DIFFUSE_METALLIC;	// 本帧G-Buffer（samplerless texelFetch读，2026-09-11降级：纯读无写入）
layout(set = 2, binding = 1)               uniform texture2D G_BUFFER_NORMAL_ROUGHNESS;	// 同上
layout(set = 2, binding = 2)               uniform texture2D G_BUFFER_EMISSION;		// 同上
layout(set = 2, binding = 3)   				uniform texture2D REPROJECTION_RESULT;				// 重投影结果	

layout(set = 2, binding = 4, rgba16f)       uniform image2D FINAL_COLOR;	                    // lighting的输出，无降噪
layout(set = 2, binding = 5, rgba16f)       uniform image2D GI_DIFFUSE_COLOR;	       			// lighting的输出，有降噪
layout(set = 2, binding = 6, rgba16f)       uniform image2D GI_SPECULAR_COLOR;	       			// lighting的输出，有降噪

float EvaluatePHat(in HitSample hitSample)
{
	return RGBtoLuminance(hitSample.outRadiance);
}