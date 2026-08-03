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

layout(location = 0) in vec4 IN_POSITION;
layout(location = 1) in vec3 IN_NORMAL;
layout(location = 2) in flat int IN_ID;

layout (location = 0) out vec4 OUT_COLOR;

void main() 
{
	VolumeLight volumeLight = LIGHTS.volumeLights[SETTING.volumeLightID];

	if(SETTING.visualizeMode == 0)
	{
		// 辐照度
		vec2 probeCoord = FetchProbeAbsTexCoord(
			normalize(IN_NORMAL), 
			IN_ID, 
			volumeLight.setting.irradianceTextureWidth, 
			volumeLight.setting.irradianceTextureHeight, 
			DDGI_IRRADIANCE_PROBE_SIZE); 

		OUT_COLOR = texture(sampler2D(VOLUME_LIGHT_IRRADIANCE[SETTING.volumeLightID], SAMPLER[0]), probeCoord);
	}
	else
	{
		// 深度
		vec2 probeCoord = FetchProbeAbsTexCoord(
		normalize(IN_NORMAL), 
		IN_ID, 
		volumeLight.setting.depthTextureWidth, 
		volumeLight.setting.depthTextureHeight, 
		DDGI_DEPTH_PROBE_SIZE); 

		OUT_COLOR = vec4(texture(sampler2D(VOLUME_LIGHT_DEPTH[SETTING.volumeLightID], SAMPLER[0]), probeCoord).r / volumeLight.setting.maxProbeDistance);
	}

	//vec3 gridCoord        = vec3(FetchProbeCoord(volumeLight.setting, IN_ID)) / volumeLight.setting.probeCounts;
	//OUT_COLOR = vec4(gridCoord, 1.0f);
}