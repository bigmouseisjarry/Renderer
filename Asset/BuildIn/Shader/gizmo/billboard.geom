#version 450
#extension GL_GOOGLE_include_directive : enable

#define ENABLE_RAY_TRACING 0
#include "../common/common.glsl"

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
layout(location = 0) in uint IN_ID[];
layout(location = 0) out uint OUT_TEXTURE_ID;
layout(location = 1) out vec2 OUT_UV;
layout(location = 2) out vec4 OUT_COLOR;

void Emit(vec3 center, vec2 extent, uint textureID, vec4 color)
{
    vec4 viewCenter = CAMERA.view * vec4(center, 1.0f);

	gl_Position = CAMERA.proj * (viewCenter + vec4(-extent.x, extent.y, 0.0f, 0.0f));
	OUT_TEXTURE_ID = textureID;
    OUT_UV = vec2(0.0f, 0.0f);
    OUT_COLOR = color;
	EmitVertex();

	gl_Position = CAMERA.proj * (viewCenter + vec4(extent.x, extent.y, 0.0f, 0.0f));
	OUT_TEXTURE_ID = textureID;
    OUT_UV = vec2(1.0f, 0.0f);
    OUT_COLOR = color;
	EmitVertex();

    gl_Position = CAMERA.proj * (viewCenter + vec4(-extent.x, -extent.y, 0.0f, 0.0f));
	OUT_TEXTURE_ID = textureID;
    OUT_UV = vec2(0.0f, 1.0f);
    OUT_COLOR = color;
	EmitVertex();

    gl_Position = CAMERA.proj * (viewCenter + vec4(extent.x, -extent.y, 0.0f, 0.0f));
	OUT_TEXTURE_ID = textureID;
    OUT_UV = vec2(1.0f, 1.0f);
    OUT_COLOR = color;
	EmitVertex();

	EndPrimitive();
}

void main()
{
	uint objectID = IN_ID[0];
    GizmoBillboardInfo info           = GIZMO_DRAW_DATA.worldBillboards[objectID];

    Emit(info.center, info.extent, info.textureID, info.color);
}

