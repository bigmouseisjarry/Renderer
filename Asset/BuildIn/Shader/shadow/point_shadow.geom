#version 450
#extension GL_GOOGLE_include_directive : enable

#define ENABLE_RAY_TRACING 0
#include "../common/common.glsl"

layout(push_constant) uniform point_light_setting {
    uint index;
    uint face;
}POINT_LIGHT_SETTING;

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
layout(location = 0) in vec3 IN_POS[];

layout(location = 0) out vec3 OUT_POS;

// 转为使用multiview扩展实现
void Emit(  in uint index, 
            in PointLight light, 
            in vec3 pos[3])
{
    gl_Layer = int(index);

    gl_Position = light.viewProj[index] * vec4(pos[0], 1.0f);
    OUT_POS     = pos[0];
    EmitVertex();

    gl_Position = light.viewProj[index] * vec4(pos[1], 1.0f);
    OUT_POS     = pos[1];
    EmitVertex();

    gl_Position = light.viewProj[index] * vec4(pos[2], 1.0f);
    OUT_POS     = pos[2];
    EmitVertex();

    EndPrimitive();
}

void main()
{
    PointLight light = LIGHTS.pointLights[POINT_LIGHT_SETTING.index];

    //可以在顶点着色器里做预投影，估计三角面三个顶点的投射位置，但是不容易做剔除（即便三个顶点都不在一个投影方向上，生成的片元还是有可能跨越这个面，而剔除需要保守）
    // Emit(0, light, IN_POS);
    // Emit(1, light, IN_POS);
    // Emit(2, light, IN_POS);
    // Emit(3, light, IN_POS);
    // Emit(4, light, IN_POS);
    // Emit(5, light, IN_POS);

    Emit(POINT_LIGHT_SETTING.face, light, IN_POS);
}