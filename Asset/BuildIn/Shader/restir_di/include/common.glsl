#define NUM_NEIGHBORS 4

#include "reservoir.glsl"

struct RestirDISetting
{
	int initialSampleCount;

	int temporalSampleCountMultiplier;
	float temporalPosThreshold;

	float spatialPosThreshold;
	float spatialNormalThreshold;
	float spatialRadius;

	uint combineMode;
    uint visibilityReuse;
    uint temporalReuse;
    uint spatialReuse;
	uint unbias;
	uint bruteLighting;
	uint showWeight;
	uint showLightID;
};

//////////////////////////////////////////////////////////////////////////

layout (set = 1, binding = 0) buffer setting
{
	RestirDISetting RESTIR_DI_SETTING;
};

layout (set = 1, binding = 1) buffer Reservoirs 
{
	DIReservoir RESERVOIRS[];                         // 本帧新生成的Reservoir(以及时域合并的结果)
};

layout (set = 1, binding = 2) buffer PrevFrameReservoirs 
{
	DIReservoir PREV_RESERVOIRS[];                    // 前一帧的Reservoir结果
};

layout (set = 1, binding = 3) buffer ResultReservoirs 
{
	DIReservoir RESULT_RESERVOIRS[];                  // 本帧Reservoir空域合并的结果
};

layout(set = 2, binding = 0, rgba8)         uniform image2D G_BUFFER_DIFFUSE_METALLIC;	        // 本帧G-Buffer
layout(set = 2, binding = 1, rgba8_snorm)   uniform image2D G_BUFFER_NORMAL_ROUGHNESS;	
layout(set = 2, binding = 2, rgba16f)       uniform image2D G_BUFFER_EMISSION;
layout(set = 2, binding = 3)   				uniform texture2D REPROJECTION_RESULT;				// 重投影结果	

layout(set = 2, binding = 4, rgba16f)       uniform image2D FINAL_COLOR;	                    // lighting的输出，无降噪
layout(set = 2, binding = 5, rgba16f)       uniform image2D DI_DIFFUSE_COLOR;	       			// lighting的输出，有降噪
layout(set = 2, binding = 6, rgba16f)       uniform image2D DI_SPECULAR_COLOR;	       			// lighting的输出，有降噪

//////////////////////////////////////////////////////////////////////////

vec3 EvaluatePHatFull(
	uint lightID, vec3 worldPos, vec3 normal, 
	vec3 diffuse, float roughness, float metallic) 
{
	vec3 N = normalize(normal);
	vec3 V = normalize(CAMERA.pos.xyz - worldPos);	

	return PointLighting(diffuse, roughness, metallic, 
						 vec4(worldPos, 1.0f), N, V, lightID);
}

float EvaluatePHat(
	uint lightID, vec3 worldPos, vec3 normal,  
	vec3 diffuse, float roughness, float metallic) 
{
	return 	RGBtoLuminance(EvaluatePHatFull(
				lightID, worldPos, normal, 
				diffuse, roughness, metallic));	
}

float HitDistance(uint lightID, vec3 worldPos)
{
	if(lightID == 0) return 0.0f;	// 0为无效ID

    PointLight pointLight = LIGHTS.pointLights[lightID];

    float pointDistance = length(worldPos.xyz - pointLight.pos);
	return pointDistance;
}