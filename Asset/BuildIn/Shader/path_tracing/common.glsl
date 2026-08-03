
struct Payload 
{
	vec3 throughput;
    vec3 lightColor;
	float distance;
    float pdf;
    vec3 reflectDir;
    uint numBounce;
    
    Rand rand;
};

layout(set = 1, binding = 0, rgba16f) uniform image2D OUT_COLOR;
layout(set = 1, binding = 1, rgba32f) uniform image2D HISTORY_COLOR;

layout(push_constant) uniform setting {
    int numSamples;
    int totalNumSamples;
    int numBounce;

    int sampleSkyBox;
    int indirectOnly;
    int diffuseOnly;
    int mode;
} SETTING;
