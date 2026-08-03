#version 460
#extension GL_GOOGLE_include_directive : enable

#include "../common/common.glsl"

layout(location = 0) out uint OUT_ID;

void main() 
{
    uint objectID               = gl_InstanceIndex;
    
    OUT_ID = objectID;
}


