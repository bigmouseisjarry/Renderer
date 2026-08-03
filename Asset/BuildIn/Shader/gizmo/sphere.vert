#version 460
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"

layout(location = 0) in vec3 IN_POS;
layout(location = 0) out vec4 OUT_COLOR;

void main() 
{
    uint objectID               = gl_InstanceIndex;
    GizmoSphereInfo info   = GIZMO_DRAW_DATA.spheres[objectID];

    OUT_COLOR                   = info.color;

    vec4 pos = vec4(IN_POS * info.radious + info.center, 1.0f);
    gl_Position = CAMERA.viewProj * pos;
}


