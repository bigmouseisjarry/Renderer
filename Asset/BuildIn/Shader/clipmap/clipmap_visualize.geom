#version 450
#extension GL_GOOGLE_include_directive : enable

#define ENABLE_RAY_TRACING 0
#include "../common/common.glsl"
#include "clipmap.glsl"

layout(push_constant) uniform clipmap_visualize_setting {
	int mipLevel;	
	int minMipLevel;
	int maxMipLevel;		
} SETTING;

layout(set = 1, binding = 0, rgba16f) uniform readonly image3D VXGI_CLIPMAP;
layout(set = 1, binding = 1) buffer VXGI_CLIPMAP_BUFFER {
	ClipmapInfo clipmapInfo;
};

layout(points) in;
layout(triangle_strip, max_vertices = 24) out;
layout(location = 0) in ivec3 IN_INDEX[];
layout(location = 0) out vec4 OUT_COLOR;
layout(location = 1) out vec2 OUT_UV;

void Emit(vec4 v0, vec4 v1, vec4 v2, vec4 v3, vec4 color)
{
	gl_Position = v0;
	OUT_COLOR = color;
	OUT_UV = vec2(0.0, 0.0);
	EmitVertex();

	gl_Position = v1;
	OUT_COLOR = color;
	OUT_UV = vec2(0.0, 1.0);
	EmitVertex();

	gl_Position = v2;
	OUT_COLOR = color;
	OUT_UV = vec2(1.0, 0.0);
	EmitVertex();

	gl_Position = v3;
	OUT_COLOR = color;
	OUT_UV = vec2(1.0, 1.0);
	EmitVertex();

	EndPrimitive();
}

void main()
{
	int mipLevel = SETTING.mipLevel;
	ClipmapLevel clipmapLevel = clipmapInfo.levels[mipLevel];

	ivec3 index = IN_INDEX[0];

	vec3 worldPos = FetchClipmapPos(clipmapLevel, index);
	vec3 halfExtent = vec3(FetchClipmapHalfExtent(clipmapLevel)); 
	float halfVoxelSize = clipmapLevel.voxelSize / 2; 

	// if (SETTING.mipLevel > 0 && 
	// 	(all(greaterThanEqual(worldPos, clipmapLevel.center.xyz - halfExtent / 2)) && 
	// 	 all(lessThan(worldPos, clipmapLevel.center.xyz + halfExtent / 2))))
	// 	return;
	
	int bestMip = FetchClipmapMipLevel(clipmapInfo, worldPos);
	if(	bestMip < SETTING.mipLevel &&
		mipLevel != SETTING.minMipLevel) return;

	// if(CAMERA.pos.y - worldPos.y < 5.0) return;

	mat4 viewProj = CAMERA.viewProj;
	vec4 v0 = viewProj * vec4(worldPos + halfVoxelSize * ivec3(-1, -1, -1), 1.0f);
	vec4 v1 = viewProj * vec4(worldPos + halfVoxelSize * ivec3(1, -1, -1), 1.0f);
	vec4 v2 = viewProj * vec4(worldPos + halfVoxelSize * ivec3(1, -1, 1), 1.0f);
	vec4 v3 = viewProj * vec4(worldPos + halfVoxelSize * ivec3(-1, -1, 1), 1.0f);
	vec4 v4 = viewProj * vec4(worldPos + halfVoxelSize * ivec3(-1, 1, -1), 1.0f);
	vec4 v5 = viewProj * vec4(worldPos + halfVoxelSize * ivec3(1, 1, -1), 1.0f);
	vec4 v6 = viewProj * vec4(worldPos + halfVoxelSize * ivec3(1, 1, 1), 1.0f);
	vec4 v7 = viewProj * vec4(worldPos + halfVoxelSize * ivec3(-1, 1, 1), 1.0f);

	// vec4 colors[6] = {
	// 	vec4(1.0f, 1.0, 1.0, 1.0),
	// 	vec4(1.0f, 1.0, 1.0, 1.0),
	// 	vec4(1.0f, 1.0, 1.0, 1.0),
	// 	vec4(1.0f, 1.0, 1.0, 1.0),
	// 	vec4(1.0f, 1.0, 1.0, 1.0),
	// 	vec4(1.0f, 1.0, 1.0, 1.0),
	// };

	vec4 colors[6] = {
		imageLoad(VXGI_CLIPMAP, FetchClipmapSampleCoord(clipmapLevel, worldPos, 0)),
		imageLoad(VXGI_CLIPMAP, FetchClipmapSampleCoord(clipmapLevel, worldPos, 1)),
		imageLoad(VXGI_CLIPMAP, FetchClipmapSampleCoord(clipmapLevel, worldPos, 2)),
		imageLoad(VXGI_CLIPMAP, FetchClipmapSampleCoord(clipmapLevel, worldPos, 3)),
		imageLoad(VXGI_CLIPMAP, FetchClipmapSampleCoord(clipmapLevel, worldPos, 4)),
		imageLoad(VXGI_CLIPMAP, FetchClipmapSampleCoord(clipmapLevel, worldPos, 5)),
	};

	if(colors[0].a >= 0.001)	Emit(v6, v5, v2, v1, colors[0]);	// 前 +X
	if(colors[1].a >= 0.001)	Emit(v4, v7, v0, v3, colors[1]);	// 后 -X
	if(colors[2].a >= 0.001)	Emit(v5, v6, v4, v7, colors[2]);	// 上 +Y
	if(colors[3].a >= 0.001)	Emit(v1, v2, v0, v3, colors[3]);	// 下 -Y
	if(colors[4].a >= 0.001)	Emit(v7, v6, v3, v2, colors[4]);	// 右 +Z
	if(colors[5].a >= 0.001)	Emit(v5, v4, v1, v0, colors[5]);	// 左 -Z

}
