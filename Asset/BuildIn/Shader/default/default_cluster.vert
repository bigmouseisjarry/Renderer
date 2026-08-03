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

const vec3 inspectColors[7] = {
	vec3(1.0f, 0.0f, 0.0f),
	vec3(0.0f, 1.0f, 0.0f),
	vec3(0.0f, 0.0f, 1.0f),
	vec3(1.0f, 1.0f, 0.0f),
	vec3(1.0f, 0.0f, 1.0f),
	vec3(0.0f, 1.0f, 1.0f),
	vec3(1.0f, 1.0f, 1.0f)
};

void main() 
{
    uint clusterID      = MESH_CLUSTER_DRAW_INFOS.slot[gl_InstanceIndex].clusterID;
    uint objectID       = MESH_CLUSTER_DRAW_INFOS.slot[gl_InstanceIndex].objectID;
    uint indexOffset    = gl_VertexIndex + MESH_CLUSTERS.slot[clusterID].indexOffset;

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
    // OUT_COLOR           = color;
    OUT_TEXCOORD        = texCoord;
    OUT_NORMAL          = worldNormal;
    OUT_TANGENT         = worldTangent;
    OUT_ID              = objectID;

    if(GLOBAL_SETTING.clusterInspectMode == 0)  OUT_COLOR = color;
    if(GLOBAL_SETTING.clusterInspectMode == 1)  OUT_COLOR = inspectColors[clusterID % 7];           // cluster索引
    if(GLOBAL_SETTING.clusterInspectMode == 2)  OUT_COLOR = inspectColors[(indexOffset / 3) % 7];   // 三角面索引
    if(GLOBAL_SETTING.clusterInspectMode == 3)  OUT_COLOR = inspectColors[clusterID % 7] * vec3(((indexOffset / 3) % 7) / 7.0f * 0.8f + 0.2f);   // 三角面索引
    
    gl_Position = CAMERA.viewProj * model * pos;
}


