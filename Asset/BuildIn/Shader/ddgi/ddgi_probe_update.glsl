

layout(push_constant) uniform DDGIComputeSetting {
	int volumeLightID;
	float _padding[3];
} SETTING;

#if defined(DEPTH_PROBE)
    #define PROBE_SIZE DDGI_DEPTH_PROBE_SIZE
    #define THREAD_SIZE_X DDGI_DEPTH_PROBE_SIZE
    #define THREAD_SIZE_Y DDGI_DEPTH_PROBE_SIZE
    #define THREAD_SIZE_Z 1	
    #define TEXTURE_WIDTH volumeLight.setting.depthTextureWidth
    #define TEXTURE_HEIGHT volumeLight.setting.depthTextureHeight
    #define TEXTURE DDGI_DEPTH
#else
    #define PROBE_SIZE DDGI_IRRADIANCE_PROBE_SIZE
    #define THREAD_SIZE_X DDGI_IRRADIANCE_PROBE_SIZE
    #define THREAD_SIZE_Y DDGI_IRRADIANCE_PROBE_SIZE
    #define THREAD_SIZE_Z 1	
    #define TEXTURE_WIDTH volumeLight.setting.irradianceTextureWidth
    #define TEXTURE_HEIGHT volumeLight.setting.irradianceTextureHeight
    #define TEXTURE DDGI_IRRADIANCE
#endif


#define CACHE_SIZE 64
shared vec4 sharedDirectionDepth[CACHE_SIZE];
#if !defined(DEPTH_PROBE) 
shared vec3 sharedRadiance[CACHE_SIZE];
#endif


void LoadCache(int probeIndex, uint offset, uint numRays) //每个线程负责一条光线信息的获取
{
    if (gl_LocalInvocationIndex < numRays)
    {
        ivec2 texCoord = ivec2(offset + uint(gl_LocalInvocationIndex), probeIndex);

        sharedDirectionDepth[gl_LocalInvocationIndex] = imageLoad(G_BUFFER_POSITION, texCoord);
    #if !defined(DEPTH_PROBE) 
        sharedRadiance[gl_LocalInvocationIndex] = imageLoad(DDGI_RADIANCE, texCoord).rgb;    
    #endif 
    }
}

const float ENERGY_CONSERVATION = 0.95;         //守恒衰减，避免光爆掉
void ProbeBlend(in DDGISetting ddgi, ivec2 currentCoord, uint numRays, inout vec3 result, inout float totalWeight)    //计算辐照度
{
    for (int i = 0; i < numRays; i++)
    {
        vec4 dirDist           = sharedDirectionDepth[i];
        vec3 rayDirection      = dirDist.xyz;
        float dist             = min(dirDist.w, ddgi.maxProbeDistance);     //限制最大深度为探针有效范围

        vec3 texelDirection    = OctDecode(FetchProbeRelativeUV(currentCoord, PROBE_SIZE));

#if !defined(DEPTH_PROBE) 
        vec3 radiance       = sharedRadiance[i] * ENERGY_CONSERVATION;   
        float weight        = max(0.0, dot(texelDirection, rayDirection));    //权重为探针像素法向量和辐射率方向向量的余弦NOL, IBL里用的是采样数
        if(weight >= FLT_EPS) 
        {
            result          += vec3(radiance * weight);                         //辐射率加权
            totalWeight     += weight;
        }
#else
        float weight        = pow(max(0.0, dot(texelDirection, rayDirection)), ddgi.depthSharpness);
        if(weight >= FLT_EPS) 
        {
            result          += vec3(dist * weight, Square(dist) * weight, 0.0); //切比雪夫加权
            totalWeight    += weight;
        }
#endif
    }
}

layout (local_size_x = THREAD_SIZE_X, 
        local_size_y = THREAD_SIZE_Y, 
        local_size_z = THREAD_SIZE_Z) in;
void main() 
{
    VolumeLight volumeLight = LIGHTS.volumeLights[SETTING.volumeLightID];
    
	// 第二轮 计算辐照度
    ivec2 currentCoord = ivec2(gl_GlobalInvocationID.xy) + (ivec2(gl_WorkGroupID.xy) * ivec2(2)) + ivec2(2);  //水平放x，y两维；垂直放z一维

    int   probeIndex = FetchPorbeIndex(currentCoord, TEXTURE_WIDTH, PROBE_SIZE);
    ivec3 probeCoord = FetchProbeCoord(volumeLight.setting, probeIndex);


    vec3 result         = vec3(0.0f);
    float weightSum     = 0.0f;
    uint remainingRays  = volumeLight.setting.raysPerProbe;
    uint offset         = 0;

    //probe上的各个像素共享所有的光线
    //以CACHE_SIZE为组共享内存，加载光线信息并计算，循环多次直至消耗所有光线
    while (remainingRays > 0)  
    {
        uint numRays = min(CACHE_SIZE, remainingRays);

        LoadCache(probeIndex, offset, numRays);
        barrier();

        ProbeBlend(volumeLight.setting, currentCoord, numRays, result, weightSum);
        barrier();

        remainingRays -= numRays;
        offset += numRays;
    }

    // Normalize the blended irradiance (or filtered distance), if the combined weight is not close to zero.
    // To match the Monte Carlo Estimator for Irradiance, we should divide by N. Instead, we are dividing by
    // N * sum(cos(theta)) (the sum of the weights) to reduce variance.
    // To account for this, we must mulitply in a factor of 1/2.
    if (weightSum > FLT_EPS) result /= weightSum * 2.0f;

    // 时域加权混合
    vec3 history = imageLoad(TEXTURE, currentCoord).rgb;
    result = mix(result, history, volumeLight.setting.blendWeight);

    // 此处计算得到的实际上是半球范围内的radiance的平均值，需要再乘半球面积2 PI来得到半球辐照度irradiance
    imageStore(TEXTURE, currentCoord, vec4( result, 0.0 ));
}



