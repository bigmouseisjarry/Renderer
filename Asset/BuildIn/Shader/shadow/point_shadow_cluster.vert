#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_multiview : enable
#extension GL_ARB_shader_viewport_layer_array : enable

#include "include/common.glsl"

// 临时形态：本pass一个render pass画6个面（j=光源*6+面），数组化按face取块——
// 刀7b-3：声明已入set2胖头（槽13，MESH_CLUSTER_DRAW_INFOS_READ[6]），本地删除
layout(push_constant) uniform point_light_setting {
    uint index;
    uint face;
}POINT_LIGHT_SETTING;

layout(location = 0) out vec3 OUT_POS;
layout(location = 1) out vec2 OUT_TEXCOORD;
layout(location = 2) out uint OUT_ID;

void main()
{
    uint clusterID      = MESH_CLUSTER_DRAW_INFOS_READ[POINT_LIGHT_SETTING.face].slot[gl_InstanceIndex].clusterID;
    uint objectID       = MESH_CLUSTER_DRAW_INFOS_READ[POINT_LIGHT_SETTING.face].slot[gl_InstanceIndex].objectID;
    uint indexOffset    = gl_VertexIndex + MESH_CLUSTERS.slot[clusterID].indexOffset;

    mat4 model          = FetchModel(objectID);
    uint index          = FetchIndex(objectID, indexOffset);
    vec4 pos            = FetchPos(objectID, index);
    vec2 texCoord       = FetchTexCoord(objectID, index);    

    vec4 worldPos       = model * pos;
    OUT_POS             = worldPos.xyz;
    OUT_TEXCOORD        = texCoord;
    OUT_ID              = objectID;

    PointLight light = LIGHTS.pointLights[POINT_LIGHT_SETTING.index];
    gl_Position = light.viewProj[POINT_LIGHT_SETTING.face] * worldPos;
    gl_Layer = int(POINT_LIGHT_SETTING.face);
}
