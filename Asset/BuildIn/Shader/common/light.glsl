#ifndef LIGHT_GLSL
#define LIGHT_GLSL

// cluster lighting 辅助函数////////////////////////////////////////////////////////////////////////////

ivec3 FetchLightClusterGrid(vec3 ndc)
{
    vec2 screenPixPos   = FetchScreenPixPos(ndc);

    float depth         = ndc.z;   
    float linearDepth   = LinearEyeDepth(depth, CAMERA.near, CAMERA.far);

    // 值 = min * pow(max/min，k/all)
    uint clusterZ       = uint(LIGHT_CLUSTER_DEPTH * log(linearDepth / CAMERA.near) / log(CAMERA.far / CAMERA.near));  //对数划分
    //uint clusterZ     = uint( (linearDepth - CAMERA.near) / ((CAMERA.far - CAMERA.near) / LIGHT_CLUSTER_DEPTH));   //均匀划分
    ivec3 clusterGrid   = ivec3(uint(screenPixPos.x / LIGHT_CLUSTER_GRID_SIZE), uint(screenPixPos.y / LIGHT_CLUSTER_GRID_SIZE), clusterZ);

    return clusterGrid;
}

ivec3 FetchLightClusterGrid(vec2 coord)
{
    float depth         = FetchDepth(coord);   
    vec3 ndc            = vec3(ScreenToNDC(coord), depth);

    return FetchLightClusterGrid(ndc);
}

uvec2 FetchLightClusterIndex(ivec3 clusterGrid)
{
    return imageLoad(LIGHT_CLUSTER_GRID, clusterGrid).xy;
}

// 阴影和衰减计算辅助函数/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

vec2 FilterPSCC(vec4 ndcPos, texture2D tex, float sunTan, float ndcZToUV, float maxRadius)
{
    ivec2 texSize = textureSize(tex, 0).xy;
    float minRadius = 1.0 / float(texSize.x);  // 保底一个texel大小
    
    // blocker 搜索半径 = 接收者深度的太阳角投影
    float searchRadius = max(ndcPos.z * ndcZToUV * sunTan, minRadius);

    float blockerSum = 0.0;
    float blockerCount = 0.0;
	for (int x = -1; x <= 1; x++)
	{
		for (int y = -1; y <= 1; y++)
		{
			float dist = texture(sampler2D(tex, SAMPLER[5]), (ndcPos.xy + vec2(1.0)) * 0.5 + vec2(x, y) * searchRadius).r;
            // 搜索的结果有效
            if (dist + 0.0002 < ndcPos.z){ blockerSum += dist; blockerCount += 1.0; }
		}
	}

    // 未被遮挡
    if (blockerCount < 0.5) return vec2(0.0, 0.0);

    // 平均被挡深度
    float avgBlocker = blockerSum / blockerCount;
    float penumbra   = (ndcPos.z - avgBlocker) * ndcZToUV * sunTan;
    
    penumbra         = clamp(penumbra, minRadius, maxRadius * minRadius);

    return vec2(penumbra, 1.0);
}

float TextureProj(vec4 ndcPos, vec2 offset, texture2D tex)
{
	float shadow = 1.0;
	if ( ndcPos.z > -1.0 && ndcPos.z < 1.0 && ndcPos.x > -0.99 && ndcPos.x < 0.99 && ndcPos.y > -0.99 && ndcPos.y < 0.99) 
	{
        // float dist = texture(sampler2D(tex, SAMPLER[0]), NDCToScreen(ndcPos.xy) + offset).r;
		float dist = texture(sampler2D(tex, SAMPLER[0]), (ndcPos.xy + vec2(1.0)) * 0.5 + offset).r;
		if ( ndcPos.z > 0.0 && dist + 0.0002 < ndcPos.z ) 	//比较的时候还得加个小bias质量更好
		{
			shadow = 0;
		}
	}
	return shadow;
}

float FilterPCF(vec4 ndcPos, texture2D tex, float maxRadius, uint count, ivec2 pixPos)
{

	float shadowFactor = 0.0;
//    if (mode == 1)  // 3x3核
//    {
//        count = 0;
//        for (int x = -1; x <= 1; x++)
//	    {
//		    for (int y = -1; y <= 1; y++)
//		    {
//			    shadowFactor += TextureProj(ndcPos, vec2(x, y) * maxRadius, tex);
//			    count++;
//		    }
//	    }
//    }

    float sqrtCount = sqrt(float(count));
    float rot = 0.0f;
//    if (mode == 2)      // IGN算旋转角
//    {
//        rot = IGNRotation(pixPos);
//    }
//    else                // STBN
//    {
//        rot = StbnRotation(pixPos);
//    }
    
    rot = StbnRotation(pixPos);
    for (int i = 0; i < count; i++)
    {
        vec2 offset = VogeLDisk(i, rot, sqrtCount);
		shadowFactor += TextureProj(ndcPos, offset * maxRadius, tex);
    }

	return shadowFactor / count;
}


float EVSM(vec4 moments, float depth, float c1, float c2)
{
	float x = exp(c1 * depth);
	float variance_x = max(moments.y - pow(moments.x, 2), 0.0001);
	float d_x = x - moments.x;  

	float p_x = variance_x / (variance_x + pow(d_x, 2)); 

	float y = exp(-c2 * depth);
	float variance_y = max(moments.w - pow(moments.z, 2), 0.0001);
	float d_y = y - moments.z;

	float p_y = variance_y / (variance_y + pow(d_y, 2));

	return min(p_x, p_y);
}

// VSM 是 x = depth 
// 现在的是EVSM的半边
float VSM(vec2 moments, float depth, float c1)
{
    // exp放大了关键部位的遮挡物的权重
    // E[exp(c * (z_r − d))] = exp(c * z_r) * E[exp(−c * d)]
	float x = exp(c1 * depth);
    // .y 平方均值  .x 均值 
    // 样本: 滤波范围内遮挡物的深度
	float variance_x = max(moments.y - pow(moments.x, 2), 0.0001);
	float d_x = x - moments.x;  

    // P(X ≥ x) ≤ σ²/(σ² + (x−μ)²)
	float p_x = variance_x / (variance_x + pow(d_x, 2)); 

	return p_x;
}


// 阴影和衰减计算////////////////////////////////////////////////////////////////////////////

int CascadeLevel(vec4 worldPos)
{
	vec3 viewPos = WorldToView(worldPos).xyz;

    int cascadeIndex = 0;
	for(int i = 0; i <= 3; i++) {
		if(-viewPos.z > LIGHTS.directionalLights[i].depth.x) {	
			cascadeIndex = i + 1;
		}
	}

	return cascadeIndex;
}

float DirectionalShadow(vec4 worldPos, ivec2 pixPos, float maxRadius)
{
	int cascadeIndex = CascadeLevel(worldPos);

    DirectionalLight dirLight = LIGHTS.directionalLights[cascadeIndex];
	if(!(dirLight.castShadow > 0)) return 1.0f;	// 不投射阴影

    vec3 dirLightVec = -normalize(dirLight.dir);    

    vec4 ndcPos = dirLight.proj * dirLight.view * worldPos;	
    ndcPos /= ndcPos.w;

    vec2 p = FilterPSCC(ndcPos, DIRECTIONAL_SHADOW[cascadeIndex],dirLight.sunTan, dirLight.ndcZToUV, maxRadius);
    if (p.y < 0.5) return 1.0f;                               // 全亮早退

    float dirShadow = FilterPCF(ndcPos, DIRECTIONAL_SHADOW[cascadeIndex], p.x, 16u, pixPos);	//计算平行光源阴影取值，[0,1]
	dirShadow = smoothstep(0.0f, 1.0f, min(1.0, dirShadow / 0.9));	                            //重映射

	return dirShadow;
}

float DirectionalShadowContrast(vec4 worldPos, texture2D histTex, int cascadeIndex)
{
    int currCascadeIndex = CascadeLevel(worldPos);
    if (currCascadeIndex != cascadeIndex) return 1.0f;   // 不同级联返回纯白

    // 这里实际上还要确定tex所属的light
    // 不过目前就比较第一个灯光的第一级联，其他直接返回纯白就行
    DirectionalLight dirLight = LIGHTS.directionalLights[currCascadeIndex];
    if(!(dirLight.castShadow > 0)) return 1.0f;

    vec4 currNdcPos = dirLight.viewProj * worldPos;	
    currNdcPos /= currNdcPos.w;
    float currDist = texture(sampler2D(DIRECTIONAL_SHADOW[cascadeIndex], SAMPLER[0]), (currNdcPos.xy + vec2(1.0)) * 0.5).r;

    vec4 histNdcPos = dirLight.prevViewProj * worldPos;
    histNdcPos /= histNdcPos.w;
    float histDist = texture(sampler2D(histTex, SAMPLER[0]), (histNdcPos.xy + vec2(1.0)) * 0.5).r;

    float currShadow = currDist + 0.0002 - currNdcPos.z;
    float histShadow = histDist + 0.0002 - histNdcPos.z;

    return (currShadow * histShadow > 0.0f) ? 0.0f : 1.0f;
}

// 获取点光源阴影值
float PointShadow(uint lightID, vec4 worldPos)
{
	if(lightID == 0) return 0.0f;	// 0为无效ID

	PointLight pointLight = LIGHTS.pointLights[lightID];
	if(pointLight.shadowID >= MAX_POINT_SHADOW_COUNT) return 1.0f;	//无效阴影索引

	vec3 pointLightVec = normalize(pointLight.pos - worldPos.xyz);
	float pointDistance = length(worldPos.xyz - pointLight.pos);

	vec2 moments = texture(samplerCube(POINT_SHADOW[pointLight.shadowID], SAMPLER[0]), -pointLightVec).xy;

	float depth = pointDistance / pointLight.far;

    float pointShadow = VSM(moments, depth, pointLight.c1);
	//float pointShadow = EVSM(moments, depth, pointLight.c1, pointLight.c2);		//计算点光源阴影取值，[0,1]
	pointShadow = smoothstep(0.0f, 1.0f, min(1.0, pointShadow / 0.8));	            //重映射，去掉大于0.8的部分

    return pointShadow;
}

float PointLightFalloff(float dist, float radius)
{
	return pow(clamp(1.0f - pow(dist / radius, 4), 0.0, 1.0), 2) / (dist * dist + 1.0f);
}


#if(ENABLE_RAY_TRACING != 0)	// 光追版本的光源阴影获取

float RtDirectionalShadow(vec4 worldPos, float bias)
{
    float dirShadow = 1.0f;
    if(LIGHTS.lightSetting.directionalLightCnt != 0)	
    {
        DirectionalLight dirLight = LIGHTS.directionalLights[0];
        vec3 L = -normalize(dirLight.dir);

        vec3 origin = worldPos.xyz + bias * L;
		if(dirLight.castShadow > 0)  dirShadow = RayQueryVisibility(origin, origin + L * MAX_RAY_TRACING_DISTANCE, 0) ? 0.0f : 1.0f; 
    }
    return dirShadow;
}

float RtPointShadow(uint lightID, vec4 worldPos)
{
    if(lightID == 0) return 0.0f;	// 0为无效ID

	PointLight pointLight = LIGHTS.pointLights[lightID];

    if(pointLight.shadowID >= MAX_POINT_SHADOW_COUNT) return 1.0f;	//无效阴影索引
    float pointShadow = RayQueryVisibility(worldPos.xyz, pointLight.pos, 0) ? 0.0f : 1.0f; 
    return pointShadow;	     // 光追就不考虑点光源是否开启投射阴影了
}

#endif



// 直接光照////////////////////////////////////////////////////////////////////////////

// 平行光源光照计算, 不计算阴影
vec3 DirectionalLighting(
    vec3 albedo, float roughness, float metallic,
    vec4 worldPos, vec3 N, vec3 V)
{
    vec3 dirColor = vec3(0.0f);
	if(LIGHTS.lightSetting.directionalLightCnt != 0)	// 目前只支持一个
    {
        DirectionalLight dirLight = LIGHTS.directionalLights[0];

        vec3 L = -normalize(dirLight.dir);    
        // 钳制为0.0到1.0
        float NoL = saturate(dot(N, L));

        float dirShadow     = 1.0f;
		//float dirShadow = DirectionalShadow(worldPos);               

        vec3 radiance = dirShadow *           //辐射率 = 阴影值 * 光强 * 颜色
                        dirLight.color * 
                        dirLight.intencity;        

        BRDFData data = ResolveBRDF(albedo, roughness, metallic, N, V, L);
        vec3 f_r = data.diffuse + data.specular;

        // 怎么反 * 总共有多少 * 这个方向摊多少
        dirColor += max(vec3(0.0f), f_r * radiance * NoL);
    }
    return dirColor;
}

// F0直存版（G-Buffer读侧）：diffuse=预乘后漫反射率，F0=写入端算好的镜面反射率，specF=虹彩F0直用
vec3 DirectionalLighting(
    vec3 diffuse, float roughness, vec3 F0, float specF,
    vec4 worldPos, vec3 N, vec3 V)
{
    vec3 dirColor = vec3(0.0f);
	if(LIGHTS.lightSetting.directionalLightCnt != 0)	// 目前只支持一个
    {
        DirectionalLight dirLight = LIGHTS.directionalLights[0];

        vec3 L = -normalize(dirLight.dir);
        float NoL = saturate(dot(N, L));

        float dirShadow     = 1.0f;

        vec3 radiance = dirShadow *
                        dirLight.color *
                        dirLight.intencity;

        BRDFData data = ResolveBRDF(diffuse, roughness, F0, specF, N, V, L);
        vec3 f_r = data.diffuse + data.specular;

        dirColor += max(vec3(0.0f), f_r * radiance * NoL);
    }
    return dirColor;
}

vec3 DirectionalDiffuseLighting(
    vec3 albedo, float roughness, float metallic,
    vec4 worldPos, vec3 N, vec3 V)
{
    vec3 dirColor = vec3(0.0f);
	if(LIGHTS.lightSetting.directionalLightCnt != 0)	// 目前只支持一个
    {
        DirectionalLight dirLight = LIGHTS.directionalLights[0];

        vec3 L = -normalize(dirLight.dir);    
        float NoL = saturate(dot(N, L));

        float dirShadow     = 1.0f;
		//float dirShadow = DirectionalShadow(worldPos);               

        vec3 radiance = dirShadow *           //辐射率 = 阴影值 * 光强 * 颜色
                        dirLight.color * 
                        dirLight.intencity;        

        vec3 f_r = ResolveBRDF(albedo, roughness, metallic, N, V, L).diffuse;

        dirColor += max(vec3(0.0f), f_r * radiance * NoL);    }
    return dirColor;
}

// 点光源光照计算, 不计算阴影
vec3 PointLighting(
    vec3 albedo, float roughness, float metallic,
    vec4 worldPos, vec3 N, vec3 V, uint lightID)
{
	if(lightID == 0) return vec3(0.0f);	// 0为无效ID

    PointLight pointLight = LIGHTS.pointLights[lightID];

    float pointDistance = length(worldPos.xyz - pointLight.pos);

    vec3 L = normalize(pointLight.pos - worldPos.xyz);
    float NoL = saturate(dot(N, L));

    float pShadow = 1.0f;
    //float pShadow       = PointShadow(lightID, worldPos);  
    float attenuation   = PointLightFalloff(pointDistance, pointLight.far);

    vec3 radiance = pShadow *               //辐射率 = 阴影值 * 光强 * 颜色 * 衰减
                    pointLight.color * 
                    pointLight.intencity *
                    attenuation;   

    BRDFData data = ResolveBRDF(albedo, roughness, metallic, N, V, L);
    vec3 f_r = data.diffuse + data.specular;

    return max(vec3(0.0f), f_r * radiance * NoL);
}

// F0直存版：单点光源
vec3 PointLighting(
    vec3 diffuse, float roughness, vec3 F0, float specF,
    vec4 worldPos, vec3 N, vec3 V, uint lightID)
{
	if(lightID == 0) return vec3(0.0f);	// 0为无效ID

    PointLight pointLight = LIGHTS.pointLights[lightID];

    float pointDistance = length(worldPos.xyz - pointLight.pos);

    vec3 L = normalize(pointLight.pos - worldPos.xyz);
    float NoL = saturate(dot(N, L));

    float pShadow = 1.0f;
    float attenuation   = PointLightFalloff(pointDistance, pointLight.far);

    vec3 radiance = pShadow *
                    pointLight.color *
                    pointLight.intencity *
                    attenuation;

    BRDFData data = ResolveBRDF(diffuse, roughness, F0, specF, N, V, L);
    vec3 f_r = data.diffuse + data.specular;

    return max(vec3(0.0f), f_r * radiance * NoL);
}

// F0直存版：cluster based
vec3 PointLighting(
    vec3 diffuse, float roughness, vec3 F0, float specF,
    vec4 worldPos, vec3 N, vec3 V)
{
    vec3 pointColor = vec3(0.0f);
    {
        ivec3 clusterGrid   = FetchLightClusterGrid(WorldToNDC(worldPos).xyz);
        uvec2 clusterIndex  = FetchLightClusterIndex(clusterGrid);

        for(uint i = clusterIndex.x; i < clusterIndex.x + clusterIndex.y; i++)
        {
            uint lightID = LIGHT_CLUSTER_INDEX.slot[i].lightID;

            pointColor  +=  PointLighting(diffuse, roughness, F0, specF, worldPos, N, V, lightID) *
                            PointShadow(lightID, worldPos);
        }
    }
	return pointColor;
}

// 点光源光照计算, 使用cluster based lighting, 计算阴影
vec3 PointLighting(
    vec3 albedo, float roughness, float metallic,
    vec4 worldPos, vec3 N, vec3 V)
{
    vec3 pointColor = vec3(0.0f);
    {
        ivec3 clusterGrid   = FetchLightClusterGrid(WorldToNDC(worldPos).xyz);
        uvec2 clusterIndex  = FetchLightClusterIndex(clusterGrid);      //读取对应cluster的索引信息

        for(uint i = clusterIndex.x; i < clusterIndex.x + clusterIndex.y; i++)
        {
            uint lightID = LIGHT_CLUSTER_INDEX.slot[i].lightID;

            pointColor  +=  PointLighting(albedo, roughness, metallic, worldPos, N, V, lightID) * 
                            PointShadow(lightID, worldPos);
        }     
    }
	return pointColor;
}

vec3 PointDiffuseLighting(
    vec3 albedo, float roughness, float metallic,
    vec4 worldPos, vec3 N, vec3 V, uint lightID)
{
	if(lightID == 0) return vec3(0.0f);	// 0为无效ID

    PointLight pointLight = LIGHTS.pointLights[lightID];

    float pointDistance = length(worldPos.xyz - pointLight.pos);

    vec3 L = normalize(pointLight.pos - worldPos.xyz);
    float NoL = saturate(dot(N, L));

    float pShadow = 1.0f;
    //float pShadow       = PointShadow(lightID, worldPos);  
    float attenuation   = PointLightFalloff(pointDistance, pointLight.far);

    vec3 radiance = pShadow *               //辐射率 = 阴影值 * 光强 * 颜色 * 衰减
                    pointLight.color * 
                    pointLight.intencity *
                    attenuation;   

    vec3 f_r = ResolveBRDF(albedo, roughness, metallic, N, V, L).diffuse;

    return max(vec3(0.0f), f_r * radiance * NoL);	
}

vec3 PointDiffuseLighting(
    vec3 albedo, float roughness, float metallic,
    vec4 worldPos, vec3 N, vec3 V)
{
    vec3 pointColor = vec3(0.0f);
    {
        ivec3 clusterGrid   = FetchLightClusterGrid(WorldToNDC(worldPos).xyz);
        uvec2 clusterIndex  = FetchLightClusterIndex(clusterGrid);      //读取对应cluster的索引信息 

        for(uint i = clusterIndex.x; i < clusterIndex.x + clusterIndex.y; i++)
        {
            uint lightID = LIGHT_CLUSTER_INDEX.slot[i].lightID;

            pointColor  +=  PointDiffuseLighting(albedo, roughness, metallic, worldPos, N, V, lightID) * 
                            PointShadow(lightID, worldPos);
        }     
    }
	return pointColor;
}

// 间接光照////////////////////////////////////////////////////////////////////////////

vec3 IndirectIrradiance(vec4 worldPos, vec3 N, vec3 V)
{
    vec3 indirectIrradiance = vec3(0.0f);
    for(int i = 0; i < LIGHTS.lightSetting.volumeLightCnt; i++)
    {
		uint volumeLightID = LIGHTS.lightSetting.volumeLightIDs[i];

        VolumeLight volumeLight = LIGHTS.volumeLights[volumeLightID];
        if(volumeLight.setting.enable &&
           PointIntersectBox(worldPos.xyz, volumeLight.setting.boundingBox))    //需要在包围盒范围内
        {
            indirectIrradiance = FetchDDGIIrradiance(	volumeLight.setting, worldPos.xyz, N, V,
														VOLUME_LIGHT_IRRADIANCE[volumeLightID], VOLUME_LIGHT_DEPTH[volumeLightID], SAMPLER[0]);
            break;  //暂时只用一个的，没处理重叠情况
        }
    }
    return indirectIrradiance;
}

vec3 IndirectLighting(
    vec3 albedo, float roughness, float metallic,
    vec4 worldPos, vec3 N, vec3 V)
{
   	float a2 			= Pow4(roughness);  
	vec3 F0 			= mix(vec3(0.04f), albedo, metallic);

    vec3 indirectColor = vec3(0.0f);          

	// DDGI
    {
        vec3 indirectIrradiance = IndirectIrradiance(worldPos, N, V);

        float NoV = saturate(dot(N, V));

        vec3 k_specular = FresnelSchlickRoughness(NoV, F0, roughness); 
        vec3 k_diffuse  = 1.0 - k_specular;     //BRDF积分项里每个radiance对应的kD是不同的（k_specular = F 视角相关），把kD移出积分是一个近似估计

        vec3 f_diffuse  = Diffuse_Lambert(albedo);  

        vec3 diffuseColor = (1.0 - metallic) * k_diffuse * f_diffuse * indirectIrradiance;      //  ?
        // vec3 diffuseColor = albedo / PI * indirectIrradiance;                                // 所有表面都当完美漫反射表面计算

        indirectColor = diffuseColor;
    }

    return indirectColor;
}


// F0直存版（G-Buffer读侧）：diffuse已预乘，不再乘(1-metallic)
vec3 IndirectLighting(
    vec3 diffuse, float roughness, vec3 F0,
    vec4 worldPos, vec3 N, vec3 V)
{
   	vec3 indirectColor = vec3(0.0f);

	// DDGI
    {
        vec3 indirectIrradiance = IndirectIrradiance(worldPos, N, V);

        float NoV = saturate(dot(N, V));

        vec3 k_specular = FresnelSchlickRoughness(NoV, F0, roughness);
        vec3 k_diffuse  = 1.0 - k_specular;     //BRDF积分项里每个radiance对应的kD是不同的（k_specular = F 视角相关），把kD移出积分是一个近似估计

        vec3 f_diffuse  = Diffuse_Lambert(diffuse);

        indirectColor = k_diffuse * f_diffuse * indirectIrradiance;
    }

    return indirectColor;
}

#endif
