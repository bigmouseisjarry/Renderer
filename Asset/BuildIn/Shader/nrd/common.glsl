
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
    uint side;               // view_z用：0=双边打包(legacy), 1=仅specular, 2=仅diffuse
} SETTING;

layout(set = 1, binding = 0)    uniform texture2D G_BUFFER_DEPTH;
layout(set = 1, binding = 1)    uniform texture2D G_BUFFER_DIFFUSE_TRANSPARENT;	
layout(set = 1, binding = 2)    uniform texture2D G_BUFFER_NORMAL_SPECF;
layout(set = 1, binding = 7)    uniform texture2D G_BUFFER_SPECULAR_F0_ROUGHNESS;	