#version 460
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"

layout(location = 0) in vec3 IN_POS;
layout(location = 0) out vec4 OUT_COLOR;

void main() 
{
    uint objectID               = gl_InstanceIndex;
    GizmoBoxInfo info           = GIZMO_DRAW_DATA.boxes[objectID];

    OUT_COLOR                   = info.color;

    vec4 pos = vec4(IN_POS * info.extent + info.center, 1.0f);
    gl_Position = CAMERA.viewProj * pos;
}


