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
#define NUM_NEIGHBORS 4
#include "reservoir.glsl"


vec3 EvaluatePHatFull(
	uint lightID, vec3 worldPos, vec3 normal,
	vec3 diffuse, float roughness, vec3 F0, float specF)
{
	vec3 N = normalize(normal);
	vec3 V = normalize(CAMERA.pos.xyz - worldPos);

	return PointLighting(diffuse, roughness, F0, specF,
						 vec4(worldPos, 1.0f), N, V, lightID);
}

float EvaluatePHat(
	uint lightID, vec3 worldPos, vec3 normal,
	vec3 diffuse, float roughness, vec3 F0, float specF)
{
	return 	RGBtoLuminance(EvaluatePHatFull(
				lightID, worldPos, normal,
				diffuse, roughness, F0, specF));
}

float HitDistance(uint lightID, vec3 worldPos)
{
	if(lightID == 0) return 0.0f;	// 0为无效ID

    PointLight pointLight = LIGHTS.pointLights[lightID];

    float pointDistance = length(worldPos.xyz - pointLight.pos);
	return pointDistance;
}
