#version 450
#extension GL_GOOGLE_include_directive : enable

#define ENABLE_RAY_TRACING 0
#include "../common/common.glsl"

layout(points) in;
layout(line_strip, max_vertices = 2) out;
layout(location = 0) in uint IN_ID[];
layout(location = 0) out vec4 OUT_COLOR;

void Emit(vec3 from, vec3 to, vec4 color)
{
	gl_Position = CAMERA.viewProj * vec4(from, 1.0f);
	OUT_COLOR = color;
	EmitVertex();

	gl_Position = CAMERA.viewProj * vec4(to, 1.0f);
	OUT_COLOR = color;
	EmitVertex();

	EndPrimitive();
}

void main()
{
	uint objectID = IN_ID[0];
    GizmoLineInfo info           = GIZMO_DRAW_DATA.lines[objectID];

    Emit(info.from, info.to, info.color);
}
