#version 450
#extension GL_GOOGLE_include_directive : enable

#define ENABLE_RAY_TRACING 0
#include "../common/common.glsl"
#include "clipmap.glsl"

layout(push_constant) uniform clipmap_visualize_setting {
	int mipLevel;			
} SETTING;

layout(set = 1, binding = 0, rgba16f) uniform readonly image3D VXGI_CLIPMAP;
layout(set = 1, binding = 1) buffer VXGI_CLIPMAP_BUFFER {
	ClipmapInfo clipmapInfo;
};

layout(location = 0) out ivec3 OUT_INDEX;

void main()
{
	int extent = clipmapInfo.levels[SETTING.mipLevel].extent;

    ivec3 index;
    index.x = gl_VertexIndex % extent;
    index.y = (gl_VertexIndex % (extent * extent)) / extent;
    index.z = gl_VertexIndex / (extent * extent);

	OUT_INDEX = index;
}
