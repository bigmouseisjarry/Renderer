
layout(push_constant) uniform NRDSetting
{
    uint isBaseColorMetalnessAvailable;
    uint enableAntiFirefly;
    uint demodulate;
    uint denoisedOnly;
    uint specularOnly;
    uint restirEnabled;
    float splitScreen;
    float motionVectorScaleX;
    float motionVectorScaleY;
    float motionVectorScaleZ;
} SETTING;

layout(set = 1, binding = 0) 			    uniform texture2D G_BUFFER_DEPTH;
layout(set = 1, binding = 1, rgba8)         uniform image2D G_BUFFER_DIFFUSE_METALLIC;	
layout(set = 1, binding = 2, rgba8_snorm)   uniform image2D G_BUFFER_NORMAL_ROUGHNESS;	