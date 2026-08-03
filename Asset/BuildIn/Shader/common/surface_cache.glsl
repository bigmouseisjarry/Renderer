#ifndef SURFACE_CACHE_GLSL
#define SURFACE_CACHE_GLSL

struct SurfaceCacheData
{
    vec3 diffuse;
    vec3 normal;
    vec4 emission;
    float roughness;
    float metallic;
};

vec4 BilinearSample(texture2D tex, sampler sampl, vec2 uv, vec4 bilinearWeights)
{
    vec4 sampleX = textureGather(sampler2D(tex, sampl), uv, 0); // textureGather指定单通道采样4临近像素
    vec4 sampleY = textureGather(sampler2D(tex, sampl), uv, 1);
    vec4 sampleZ = textureGather(sampler2D(tex, sampl), uv, 2);
    vec4 sampleW = textureGather(sampler2D(tex, sampl), uv, 3);
    return vec4(dot(sampleX, bilinearWeights), 
                dot(sampleY, bilinearWeights), 
                dot(sampleZ, bilinearWeights),
                dot(sampleW, bilinearWeights));
}

// 采样单个card的数据存储在data中，返回混合权重
float FetchMeshCardData(inout SurfaceCacheData data, uint meshCardID, vec3 localPos, vec3 localNormal, float normalWeight)    
{
    MeshCardInfo card = MESH_CARDS.slot[meshCardID];
    if(card.sampleAtlasExtent == uvec2(0)) return 0.0f;    // 未分配
    
    vec3 worldPos = localPos * card.scale;           // 在card和对应的虚拟相机所在空间进行后续计算
    vec3 ndcPos = (card.viewProj * vec4(worldPos, 1.0f)).xyz;
    vec2 texCoord = ndcPos.xy * 0.5f + 0.5f;
    float depth = ndcPos.z;

    vec2 atlasCoord = (texCoord * card.sampleAtlasExtent + card.sampleAtlasOffset) / SURFACE_CACHE_SIZE;    // 需要手动做双线性插值
    // Calculate bilinear weights
    vec2 bilinearWeightsUV = fract(texCoord * card.sampleAtlasExtent + 0.5f);
    vec4 bilinearWeights;
    bilinearWeights.x = (1.0 - bilinearWeightsUV.x) * (bilinearWeightsUV.y);
    bilinearWeights.y = (bilinearWeightsUV.x) * (bilinearWeightsUV.y);
    bilinearWeights.z = (bilinearWeightsUV.x) * (1 - bilinearWeightsUV.y);
    bilinearWeights.w = (1 - bilinearWeightsUV.x) * (1 - bilinearWeightsUV.y);

    vec4 sampleDepth = textureGather(sampler2D(SURFACE_CACHE[4], SAMPLER[1]), atlasCoord, 0);
    float depthThreshold = 0.5f / card.viewExtent.z;    // 深度的最大误差，可以设置成sdf体素尺寸等，这里取+-0.5体素深度为bias，超过+-1.0体素深度不可用
    vec4 depthVisibility = vec4(1.0f);
    for (uint i = 0; i < 4; i++)
    {
        depthVisibility[i] = 1.0f - clamp((abs(sampleDepth[i] - depth) - depthThreshold) / depthThreshold, 0, 1);
        if (sampleDepth[i] >= 0.9999f)
            depthVisibility[i] = 0.0f;
    }
    float sampleWeight = dot(depthVisibility, bilinearWeights); // 2. 深度权重和双线性插值权重
    sampleWeight *= normalWeight;
    if (sampleWeight <= 0.0f)   return 0.0f;    
            
    bilinearWeights *= depthVisibility;

    vec4 diffuseMetallic    = BilinearSample(SURFACE_CACHE[0], SAMPLER[1], atlasCoord, bilinearWeights);
    vec4 normalRoughness    = BilinearSample(SURFACE_CACHE[1], SAMPLER[1], atlasCoord, bilinearWeights);
    vec4 emission           = BilinearSample(SURFACE_CACHE[2], SAMPLER[1], atlasCoord, bilinearWeights);

    data.diffuse += diffuseMetallic.rgb * sampleWeight;
    data.roughness += normalRoughness.a * sampleWeight;
    data.normal += normalize(normalRoughness.rgb) * sampleWeight;
    data.metallic += diffuseMetallic.a * sampleWeight;
    data.emission += emission * sampleWeight;
    return sampleWeight;
}

// 根据hit信息和物体id采样三个card，返回是否有效
bool FetchSurfaceCacheData(out SurfaceCacheData data, uint objectID, vec3 hitPos, vec3 hitNormal)
{
    data.diffuse = vec3(0.0f);
    data.roughness = 0.0f;
    data.normal = vec3(0.0f);
    data.metallic = 0.0f;
    data.emission = vec4(0.0f);

    if(objectID == 0) return false;
    Object object = OBJECTS.slot[objectID];

    vec3 localNormal = normalize(mat3(object.invModel) * hitNormal);
    vec3 localPos    = (object.invModel * vec4(hitPos, 1.0f)).xyz;
    ivec3 samples = ivec3(
        localNormal.x > 0 ? 0 : 1,
        localNormal.y > 0 ? 2 : 3,
        localNormal.z > 0 ? 4 : 5);
    float totalWeight = 0.0f;
    for(int i = 0; i < 3; i++)  
    {
        float normalWeight = pow(abs(localNormal[i]), 2);               // 1. 根据hitNormal选出三个card，按照法线投影长度作为权重进行采样加权
        // normalWeight = (normalWeight - 0.025f) / (1.0f - 0.025f);    // 测试这样写效果还行
        // if (normalWeight <= 0.0f) continue;
        totalWeight += FetchMeshCardData(data, object.meshCardID + samples[i], localPos, localNormal, normalWeight);
    }
    if(totalWeight > 0.001f)
    {
        data.diffuse /= totalWeight;
        data.roughness /= totalWeight;
        data.normal /= totalWeight;
        data.metallic /= totalWeight;
        data.emission /= totalWeight;
    }
    return totalWeight > 0.001f;
}


float FetchMeshCardLighting(inout vec4 lighting, uint meshCardID, vec3 localPos, vec3 localNormal, float normalWeight)    
{
    MeshCardInfo card = MESH_CARDS.slot[meshCardID];
    if(card.sampleAtlasExtent == uvec2(0)) return 0.0f;    // 未分配
    MESH_CARD_READBACKS.slot[meshCardID] = int(GLOBAL_SETTING.totalTicks);   // 写入当前card的最近使用时间
    
    vec3 worldPos = localPos * card.scale;           // 在card和对应的虚拟相机所在空间进行后续计算
    vec3 ndcPos = (card.viewProj * vec4(worldPos, 1.0f)).xyz;
    vec2 texCoord = ndcPos.xy * 0.5f + 0.5f;
    float depth = ndcPos.z;

    vec2 atlasCoord = (texCoord * card.sampleAtlasExtent + card.sampleAtlasOffset) / SURFACE_CACHE_SIZE;    // 需要手动做双线性插值
    // Calculate bilinear weights
    vec2 bilinearWeightsUV = fract(texCoord * card.sampleAtlasExtent + 0.5f);
    vec4 bilinearWeights;
    bilinearWeights.x = (1.0 - bilinearWeightsUV.x) * (bilinearWeightsUV.y);
    bilinearWeights.y = (bilinearWeightsUV.x) * (bilinearWeightsUV.y);
    bilinearWeights.z = (bilinearWeightsUV.x) * (1 - bilinearWeightsUV.y);
    bilinearWeights.w = (1 - bilinearWeightsUV.x) * (1 - bilinearWeightsUV.y);

    vec4 sampleDepth = textureGather(sampler2D(SURFACE_CACHE[4], SAMPLER[1]), atlasCoord, 0);
    float depthThreshold = 0.5f / card.viewExtent.z;    // 深度的最大误差，可以设置成sdf体素尺寸等，这里取+-0.5体素深度为bias，超过+-1.0体素深度不可用
    vec4 depthVisibility = vec4(1.0f);
    for (uint i = 0; i < 4; i++)
    {
        depthVisibility[i] = 1.0f - clamp((abs(sampleDepth[i] - depth) - depthThreshold) / depthThreshold, 0, 1);
        if (sampleDepth[i] >= 0.99f)
            depthVisibility[i] = 0.0f;
    }
    float sampleWeight = dot(depthVisibility, bilinearWeights); // 2. 深度权重和双线性插值权重
    sampleWeight *= normalWeight;
    if (sampleWeight <= 0.0f)   return 0.0f;    
            
    bilinearWeights *= depthVisibility;

    lighting += BilinearSample(SURFACE_CACHE[3], SAMPLER[1], atlasCoord, bilinearWeights) * sampleWeight;
    return sampleWeight;
}

bool FetchSurfaceCacheLighting(out vec4 lighting, uint objectID, vec3 hitPos, vec3 hitNormal)
{
    lighting = vec4(0.0f);

    if(objectID == 0) return false;
    Object object = OBJECTS.slot[objectID];

    vec3 localNormal = normalize(mat3(object.invModel) * hitNormal);
    vec3 localPos    = (object.invModel * vec4(hitPos, 1.0f)).xyz;
    ivec3 samples = ivec3(
        localNormal.x > 0 ? 0 : 1,
        localNormal.y > 0 ? 2 : 3,
        localNormal.z > 0 ? 4 : 5);
    float totalWeight = 0.0f;
    for(int i = 0; i < 3; i++)  
    {
        float normalWeight = pow(abs(localNormal[i]), 2);               // 1. 根据hitNormal选出三个card，按照法线投影长度作为权重进行采样加权
        // normalWeight = (normalWeight - 0.025f) / (1.0f - 0.025f);    // 测试这样写效果还行
        // if (normalWeight <= 0.0f) continue;
        totalWeight += FetchMeshCardLighting(lighting, object.meshCardID + samples[i], localPos, localNormal, normalWeight);
    }
    return totalWeight > 0.001f;
}


#endif