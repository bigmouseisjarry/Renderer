#version 460
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"

layout(push_constant) uniform SurfaceCacheRasterizeSetting {
    uint meshCardID;
    uint objectID;
    uint vertexID;
} SETTING;

layout(location = 0) out vec3 OUT_COLOR;
layout(location = 1) out vec2 OUT_TEXCOORD;
layout(location = 2) out vec3 OUT_NORMAL;
layout(location = 3) out flat uint OUT_ID;

void main() 
{
    uint objectID       = SETTING.objectID;
    uint indexOffset    = gl_VertexIndex;

    MeshCardInfo card   = MESH_CARDS.slot[SETTING.meshCardID];



    vec4 position   = FetchVertexPos(SETTING.vertexID, indexOffset);
    position.xyz    *= card.scale;
    vec3 normal     = FetchVertexNormal(SETTING.vertexID, indexOffset);
    vec2 texCoord   = FetchVertexTexCoord(SETTING.vertexID, indexOffset);
    vec3 color      = FetchVertexColor(SETTING.vertexID, indexOffset);

    OUT_COLOR           = color;
    OUT_TEXCOORD        = texCoord;
    OUT_NORMAL          = normal;
    OUT_ID              = objectID;

    gl_Position = card.viewProj * position;
}


