#version 460
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"

layout(location = 0) out vec4 OUT_POSITION;
layout(location = 1) out vec4 OUT_PREV_POSITION;
layout(location = 2) out vec3 OUT_COLOR;
layout(location = 3) out vec2 OUT_TEXCOORD;
layout(location = 4) out vec3 OUT_NORMAL;
layout(location = 5) out vec4 OUT_TANGENT;
layout(location = 6) out flat uint OUT_ID;

void main() 
{
    uint objectID       = gl_InstanceIndex;
    uint indexOffset    = gl_VertexIndex;

    mat4 model          = FetchModel(objectID);
    mat4 prevModel      = FetchPrevModel(objectID);
    uint index          = FetchIndex(objectID, indexOffset);
    vec4 pos            = FetchPos(objectID, index);
    vec3 worldNormal    = FetchWorldNormal(FetchNormal(objectID, index), model);
    vec4 worldTangent   = FetchWorldTangent(FetchTangent(objectID, index), model);
    vec3 color          = FetchColor(objectID, index); 
    vec2 texCoord       = FetchTexCoord(objectID, index);      

    OUT_POSITION        = model * pos;
    OUT_PREV_POSITION   = prevModel * pos;
    OUT_COLOR           = color;
    OUT_TEXCOORD        = texCoord;
    OUT_NORMAL          = worldNormal;
    OUT_TANGENT         = worldTangent;
    OUT_ID              = objectID;

    gl_Position = CAMERA.viewProj * model * pos;
}


