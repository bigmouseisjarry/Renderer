

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

// shared 变量是每个 workgroup 一份私有拷贝
#define CACHE_SIZE 64
shared vec4 sharedDirectionDepth[CACHE_SIZE];
#if !defined(DEPTH_PROBE) 
shared vec3 sharedRadiance[CACHE_SIZE];
#endif


void LoadCache(int probeIndex, uint offset, uint numRays) //每个线程负责一条光线信息的获取
{
    // 确保有序填充
    if (gl_LocalInvocationIndex < numRays)
    {
        ivec2 texCoord = ivec2(offset + uint(gl_LocalInvocationIndex), probeIndex);

        sharedDirectionDepth[gl_LocalInvocationIndex] = imageLoad(TRACE_DIRECTION_DIST, texCoord);
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
        // dot将背部半球裁掉
        float weight        = max(0.0, dot(texelDirection, rayDirection));    //权重为探针像素法向量和辐射率方向向量的余弦NOL, IBL里用的是采样数
        if(weight >= FLT_EPS) 
        {
            // L(ω) * (ω * d)
            result          += vec3(radiance * weight);                         //辐射率加权
            totalWeight     += weight;
        }
#else
        float weight        = pow(max(0.0, dot(texelDirection, rayDirection)), ddgi.depthSharpness);
        if(weight >= FLT_EPS) 
        {
                                    // m1          m2
            result          += vec3(dist * weight, Square(dist) * weight, 0.0); //切比雪夫加权
            totalWeight    += weight;
        }
#endif
    }
}


// E(d) = ∫_hemisphere(d)  L(ω) * (ω * d) dω
// 
// d                     假想表面的法线
// ω                    某个入射方向
// L(ω)                 从方向 ω 来的辐亮度
// (ω * d)              入射角余弦
// hemisphere(d)         只积以 d 为轴的半个球
// dω                   微元立体角
// E(d)                  朝向 d的表面收到的总光

layout (local_size_x = THREAD_SIZE_X, 
        local_size_y = THREAD_SIZE_Y, 
        local_size_z = THREAD_SIZE_Z) in;
void main() 
{
    VolumeLight volumeLight = LIGHTS.volumeLights[SETTING.volumeLightID];
    
	// 第二轮 计算辐照度
    // 没有补齐的全局pixel位置 + 每个小纹理间的2padding + 当前小纹理前的2padding
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
        // barrier() 的同步范围是一个 workgroup
        barrier();

        ProbeBlend(volumeLight.setting, currentCoord, numRays, result, weightSum);
        barrier();

        remainingRays -= numRays;
        offset += numRays;
    }

    // 从除以采样数到除以权重和
    // 蒙特卡洛积分原公式
    // E ≈ (1/N) * ΣLᵢwᵢ / p = (4π/N) * ΣLᵢwᵢ        wᵢ = (ωᵢ·d)⁺   // p 恒定（均匀全球面采样 pdf = 1/4π）

    // 对均匀全球面射线算余弦权重的期望
    // E[w] = (1/4π)·∫_sphere (ω·d)⁺ dω = (1/4π)·∫_hemisphere cosθ dω = (1/4π)·π = 1/4

    // 有ΣLw ≈ N·E/(4π) 和 Σw ≈ N/4
    // 所以ΣLw/Σw = [N·E/(4π)] / (N/4) = E/π

    // 所以结果之间就差一个PI，而PI被分成两半存放，这里多除以2是因为好存

    if (weightSum > FLT_EPS) result /= weightSum * 2.0f;

    // 时域加权混合
    vec3 history = imageLoad(TEXTURE, currentCoord).rgb;
    result = mix(result, history, volumeLight.setting.blendWeight);

    // 此处计算得到的实际上是半球范围内的radiance的平均值，需要再乘半球面积2 PI来得到半球辐照度irradiance
    imageStore(TEXTURE, currentCoord, vec4( result, 0.0 ));
}



