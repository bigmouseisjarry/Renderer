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


// 阴影和衰减计算////////////////////////////////////////////////////////////////////////////


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
        LightStructGPU dirLight = LIGHTS.slot[0];
        vec3 L = -normalize(dirLight.dir_tan.xyz);

        vec3 origin = worldPos.xyz + bias * L;
		// cullMask=shadow caster位：只被"材质CastShadow"的instance遮挡，与光栅CSM/点光阴影的语义对齐
		if(LightCastShadow(dirLight))  dirShadow = RayQueryVisibility(origin, origin + L * MAX_RAY_TRACING_DISTANCE, 0, INSTANCE_MASK_SHADOW_CASTER) ? 0.0f : 1.0f;
    }
    return dirShadow;
}

float RtPointShadow(uint lightID, vec4 worldPos)
{
    if(lightID == 0) return 0.0f;	// 0为无效ID

	LightStructGPU pointLight = LIGHTS.slot[lightID];

    if(!LightCastShadow(pointLight)) return 1.0f;	// 灯布局刀②：castShadow位取代shadowID槽判断
    float pointShadow = RayQueryVisibility(worldPos.xyz, pointLight.pos_range.xyz, 0, INSTANCE_MASK_SHADOW_CASTER) ? 0.0f : 1.0f;
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
        LightStructGPU dirLight = LIGHTS.slot[0];

        vec3 L = -normalize(dirLight.dir_tan.xyz);    
        // 钳制为0.0到1.0
        float NoL = saturate(dot(N, L));

        float dirShadow     = 1.0f;
		//float dirShadow = DirectionalShadow(worldPos);               

        vec3 radiance = dirShadow *           //辐射率 = 阴影值 * 光强 * 颜色
                        dirLight.color_intencity.rgb * 
                        dirLight.color_intencity.w;        

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
        LightStructGPU dirLight = LIGHTS.slot[0];

        vec3 L = -normalize(dirLight.dir_tan.xyz);
        float NoL = saturate(dot(N, L));

        float dirShadow     = 1.0f;

        vec3 radiance = dirShadow *
                        dirLight.color_intencity.rgb *
                        dirLight.color_intencity.w;

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
        LightStructGPU dirLight = LIGHTS.slot[0];

        vec3 L = -normalize(dirLight.dir_tan.xyz);    
        float NoL = saturate(dot(N, L));

        float dirShadow     = 1.0f;
		//float dirShadow = DirectionalShadow(worldPos);               

        vec3 radiance = dirShadow *           //辐射率 = 阴影值 * 光强 * 颜色
                        dirLight.color_intencity.rgb * 
                        dirLight.color_intencity.w;        

        vec3 f_r = ResolveBRDF(albedo, roughness, metallic, N, V, L).diffuse;

        dirColor += max(vec3(0.0f), f_r * radiance * NoL);    
     }
    return dirColor;
}

// 点光源光照计算, 不计算阴影
vec3 PointLighting(
    vec3 albedo, float roughness, float metallic,
    vec4 worldPos, vec3 N, vec3 V, uint lightID)
{
	if(lightID == 0) return vec3(0.0f);	// 0为无效ID

    LightStructGPU pointLight = LIGHTS.slot[lightID];

    float pointDistance = length(worldPos.xyz - pointLight.pos_range.xyz);

    vec3 L = normalize(pointLight.pos_range.xyz - worldPos.xyz);
    float NoL = saturate(dot(N, L));

    float pShadow = 1.0f;
    //float pShadow       = PointShadow(lightID, worldPos);  
    float attenuation   = PointLightFalloff(pointDistance, pointLight.pos_range.w);

    vec3 radiance = pShadow *               //辐射率 = 阴影值 * 光强 * 颜色 * 衰减
                    pointLight.color_intencity.rgb * 
                    pointLight.color_intencity.w *
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

    LightStructGPU pointLight = LIGHTS.slot[lightID];

    float pointDistance = length(worldPos.xyz - pointLight.pos_range.xyz);

    vec3 L = normalize(pointLight.pos_range.xyz - worldPos.xyz);
    float NoL = saturate(dot(N, L));

    float pShadow = 1.0f;
    float attenuation   = PointLightFalloff(pointDistance, pointLight.pos_range.w);

    vec3 radiance = pShadow *
                    pointLight.color_intencity.rgb *
                    pointLight.color_intencity.w *
                    attenuation;

    BRDFData data = ResolveBRDF(diffuse, roughness, F0, specF, N, V, L);
    vec3 f_r = data.diffuse + data.specular;

    return max(vec3(0.0f), f_r * radiance * NoL);
}

#if(ENABLE_RAY_TRACING != 0)    // 阴影转换刀B：cluster循环版阴影依赖RtPointShadow，RT=0编译单元（gizmo/clipmap/shadow.geom）不实例化
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
                            RtPointShadow(lightID, worldPos);    // 阴影转换刀B：cube VSM退役中（消费者=deferred_lighting/ReSTIR_DI）
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
                            RtPointShadow(lightID, worldPos);    // 阴影转换刀B
        }     
    }
	return pointColor;
}

#endif
vec3 PointDiffuseLighting(
    vec3 albedo, float roughness, float metallic,
    vec4 worldPos, vec3 N, vec3 V, uint lightID)
{
	if(lightID == 0) return vec3(0.0f);	// 0为无效ID

    LightStructGPU pointLight = LIGHTS.slot[lightID];

    float pointDistance = length(worldPos.xyz - pointLight.pos_range.xyz);

    vec3 L = normalize(pointLight.pos_range.xyz - worldPos.xyz);
    float NoL = saturate(dot(N, L));

    float pShadow = 1.0f;
    //float pShadow       = PointShadow(lightID, worldPos);  
    float attenuation   = PointLightFalloff(pointDistance, pointLight.pos_range.w);

    vec3 radiance = pShadow *               //辐射率 = 阴影值 * 光强 * 颜色 * 衰减
                    pointLight.color_intencity.rgb * 
                    pointLight.color_intencity.w *
                    attenuation;   

    vec3 f_r = ResolveBRDF(albedo, roughness, metallic, N, V, L).diffuse;

    return max(vec3(0.0f), f_r * radiance * NoL);	
}

#if(ENABLE_RAY_TRACING != 0)    
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
                            RtPointShadow(lightID, worldPos);    // 阴影转换刀B（当前零外部调用者，随族换）
        }     
    }
	return pointColor;
}

#endif

// 间接光照////////////////////////////////////////////////////////////////////////////

vec3 IndirectIrradiance(vec4 worldPos, vec3 N, vec3 V)
{
    vec3 indirectIrradiance = vec3(0.0f);
    for(int i = 0; i < VOLUME_LIGHT_SETTINGS.volumeLightCnt; i++)
    {
		uint volumeLightID = VOLUME_LIGHT_SETTINGS.volumeLightIDs[i];

        VolumeLight volumeLight = VOLUME_LIGHT_SETTINGS.volumeLights[volumeLightID];
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
