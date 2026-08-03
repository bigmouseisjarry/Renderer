
layout(set = 1, binding = 0)                uniform texture2D G_BUFFER_DIFFUSE_METALLIC;	
layout(set = 1, binding = 1)                uniform texture2D G_BUFFER_NORMAL_ROUGHNESS;	
layout(set = 1, binding = 2)   		        uniform texture2D HISTORY_G_BUFFER_NORMAL_ROUGHNESS;	
//layout(set = 1, binding = 2)                uniform texture2D REPROJECTION_RESULT;	

layout(set = 1, binding = 3, rgba16f) 		uniform image2D IN_COLOR;	
layout(set = 1, binding = 4, rgba16f) 		uniform image2D OUT_COLOR;	

layout(set = 1, binding = 5) 				uniform texture2D HISTORY_MOMENTS;				// 前两维moments，第三维history length，//第四维预滤波variance
layout(set = 1, binding = 6, rgba32f) 		uniform image2D CURRENT_MOMENTS;                 
layout(set = 1, binding = 7) 				uniform texture2D HISTORY_COLOR_VARIANCE;		// 前三维color，第四维variance
layout(set = 1, binding = 8, rgba16f) 		uniform image2D CURRENT_COLOR_VARIANCE[2];		// 需要pingpong	

layout(push_constant) uniform SVGFSetting 
{
    int round;      // 论文给出的参数设置：
    int maxRound;   // 5
       
    float sigmaP;   // 1.0  
    float sigmaN;   // 128.0
    float sigmaL;   // 4.0

    float alpha;
    int mode;

    int denoisedOnly;
    int disocclusionFix;
    int antiFirefly;
    int historyClamp;

} SETTING;

const float epsilon = 0.00001;

//const float h[5] = float[5](1.0 / 16.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0);
// const float h[25] = float[25](
//     1.0 / 256.0, 1.0 / 64.0, 3.0 / 128.0, 1.0 / 64.0, 1.0 / 256.0,
//     1.0 / 64.0,  1.0 / 16.0, 3.0 / 32.0,  1.0 / 16.0, 1.0 / 64.0,
//     3.0 / 128.0, 3.0 / 32.0, 9.0 / 64.0,  3.0 / 32.0, 3.0 / 128.0,
//     1.0 / 64.0,  1.0 / 16.0, 3.0 / 32.0,  1.0 / 16.0, 1.0 / 64.0,
//     1.0 / 256.0, 1.0 / 64.0, 3.0 / 128.0, 1.0 / 64.0, 1.0 / 256.0
// );

const float gaussKernel3x3[9] = float[9](
    1, 2, 1, 
    2, 4, 2, 
    1, 2, 1
);  // 16

const float gaussKernel5x5[25] = float[25](
    1, 4,  7,  4,  1,
    4, 16, 26, 16, 4,
    7, 26, 41, 26, 7, 
    4, 16, 26, 16, 4,    
    1, 4,  7,  4,  1
);  // 273

// const float gaussKernel7x7[49] = float[49](
//     0, 0,  1,  2,   1,  0,  0,
//     0, 3,  13, 22,  13, 3,  0,
//     1, 13, 59, 97,  59, 13, 1,
//     2, 22, 97, 159, 97, 22, 2,
//     1, 13, 59, 97,  59, 13, 1,
//     0, 3,  13, 22,  13, 3,  0,
//     0, 0,  1,  2,   1,  0,  0
// );  // 1003

float GetPlaneDistanceWeight(vec3 centerWorldPos, vec3 centerNormal, vec3 sampleWorldPos)  
{
    float distanceToCenterPointPlane = abs(dot(sampleWorldPos - centerWorldPos, centerNormal));     // 原论文是深度项，ReLAX使用的是点面距离

    return distanceToCenterPointPlane < SETTING.sigmaP ? 1.0 : 0.0;
}

float GetLuminanceWeight(float centerLuminance, float sampleLuminance, float variance)
{
    return min(exp(-abs(centerLuminance - sampleLuminance) / (SETTING.sigmaL * sqrt(variance) + epsilon)), 1.0f);  // 亮度项
}

float GetNormalWeight(vec3 centerNormal, vec3 sampleNormal)
{
    return pow(max(0.0f, dot(centerNormal, sampleNormal)), SETTING.sigmaN);
}