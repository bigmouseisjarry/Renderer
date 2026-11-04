#ifndef LIGHT_GLSL
#define LIGHT_GLSL

// cluster lighting 辅助函数////////////////////////////////////////////////////////////////////////////

uint FetchLightTileEntries(vec3 ndc, out uint lightCount, out uint binBit)
{
    vec2 screenPixPos   = FetchScreenPixPos(ndc.xy);
    uvec2 tileId        = uvec2(uint(screenPixPos.x / LIGHT_CLUSTER_GRID_SIZE),
                                uint(screenPixPos.y / LIGHT_CLUSTER_GRID_SIZE));

    float linearDepth   = LinearEyeDepth(ndc.z, CAMERA.near, CAMERA.far);
    uint binIdx         = min(uint(ZBIN_BIN_COUNT * log(linearDepth / CAMERA.near) / log(CAMERA.far / CAMERA.near)),
                              uint(ZBIN_BIN_COUNT) - 1u);    // 夹23——1u<<24会撞entry的index字段
    binBit              = 1u << binIdx;

    uint start          = (tileId.x + tileId.y * uint(LIGHT_CLUSTER_WIDTH)) * (ZBIN_LIGHTS_PER_TILE + 1u);
    lightCount          = min(TILE_LIGHTS.words[start], uint(ZBIN_LIGHTS_PER_TILE));   // 构建端已预截断，此处min=双保险
    return start + 1u;
}


// 阴影和衰减计算////////////////////////////////////////////////////////////////////////////


float PointLightFalloff(float dist, float radius)
{
	return pow(clamp(1.0f - pow(dist / radius, 4), 0.0, 1.0), 2) / (dist * dist + 1.0f);
}

struct SolvedLight
{
	vec3 dir;		// 表面指向灯
	vec3 end;		// 阴影射线终点（局部灯=灯位置；方向灯=pos+dir*MAX_RAY_TRACING_DISTANCE）
	vec3 radiance;	// 颜色×强度×距离衰减
};

SolvedLight SolveLight(LightStructGPU light, vec3 pos)
{
	SolvedLight s;
	vec3 toLight   = light.position_range.xyz - pos;
	float dist     = length(toLight);
	s.dir          = toLight / max(dist, 1e-5f);
	s.end          = light.position_range.xyz;
	s.radiance     = light.radiance_type.rgb * PointLightFalloff(dist, light.position_range.w);
	return s;
}

// 方向灯版：HRP把无限远近似成MAX_RAY_TRACING_DISTANCE外一点（阴影射线终点+角抖动横移的锚）
SolvedLight SolveDirectionalLight(LightStructGPU light, vec3 pos)
{
	SolvedLight s;
	s.dir      = -normalize(light.mainDirection_id.xyz);
	s.end      = pos + s.dir * MAX_RAY_TRACING_DISTANCE;
	s.radiance = light.radiance_type.rgb;
	return s;
}


#if(ENABLE_RAY_TRACING != 0)	// 光追版本的光源阴影获取

float RtDirectionalShadow(vec4 worldPos, float bias)
{
    float dirShadow = 1.0f;
    if(LIGHTS.lightSetting.directionalLightCnt != 0)	
    {
        LightStructGPU dirLight = LIGHTS.slot[0];
        vec3 L = -normalize(dirLight.mainDirection_id.xyz);

        vec3 origin = worldPos.xyz + bias * L;
		// cullMask=shadow caster位：只被"材质CastShadow"的instance遮挡，与光栅CSM/点光阴影的语义对齐
		if(LightCastShadow(dirLight))  dirShadow = RayQueryVisibility(origin, origin + L * MAX_RAY_TRACING_DISTANCE, 0, INSTANCE_MASK_SHADOW_CASTER) ? 0.0f : 1.0f;
    }
    return dirShadow;
}

float RtPointShadow(uint lightID, vec4 worldPos)
{
    if(lightID == LIGHT_ID_INVALID) return 0.0f;	

	LightStructGPU pointLight = LIGHTS.slot[lightID];

    if(!LightCastShadow(pointLight)) return 1.0f;	
    float pointShadow = RayQueryVisibility(worldPos.xyz, pointLight.position_range.xyz, 0, INSTANCE_MASK_SHADOW_CASTER) ? 0.0f : 1.0f;
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

        vec3 L = -normalize(dirLight.mainDirection_id.xyz);    
        // 钳制为0.0到1.0
        float NoL = saturate(dot(N, L));

        float dirShadow     = 1.0f;
		//float dirShadow = DirectionalShadow(worldPos);               

        vec3 radiance = dirShadow *           //辐射率 = 阴影值 * 光强 * 颜色
                        dirLight.radiance_type.rgb;        

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

        vec3 L = -normalize(dirLight.mainDirection_id.xyz);
        float NoL = saturate(dot(N, L));

        float dirShadow     = 1.0f;

        vec3 radiance = dirShadow *
                        dirLight.radiance_type.rgb;

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

        vec3 L = -normalize(dirLight.mainDirection_id.xyz);    
        float NoL = saturate(dot(N, L));

        float dirShadow     = 1.0f;
		//float dirShadow = DirectionalShadow(worldPos);               

        vec3 radiance = dirShadow *           //辐射率 = 阴影值 * 光强 * 颜色
                        dirLight.radiance_type.rgb;        

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
	if(lightID == LIGHT_ID_INVALID) return vec3(0.0f);	

    LightStructGPU pointLight = LIGHTS.slot[lightID];

    float pointDistance = length(worldPos.xyz - pointLight.position_range.xyz);

    vec3 L = normalize(pointLight.position_range.xyz - worldPos.xyz);
    float NoL = saturate(dot(N, L));

    float pShadow = 1.0f;
    //float pShadow       = PointShadow(lightID, worldPos);  
    float attenuation   = PointLightFalloff(pointDistance, pointLight.position_range.w);

    vec3 radiance = pShadow *               //辐射率 = 阴影值 * 光强 * 颜色 * 衰减
                    pointLight.radiance_type.rgb *
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
	if(lightID == LIGHT_ID_INVALID) return vec3(0.0f);

    LightStructGPU pointLight = LIGHTS.slot[lightID];

    float pointDistance = length(worldPos.xyz - pointLight.position_range.xyz);

    vec3 L = normalize(pointLight.position_range.xyz - worldPos.xyz);
    float NoL = saturate(dot(N, L));

    float pShadow = 1.0f;
    float attenuation   = PointLightFalloff(pointDistance, pointLight.position_range.w);

    vec3 radiance = pShadow *
                    pointLight.radiance_type.rgb *
                    attenuation;

    BRDFData data = ResolveBRDF(diffuse, roughness, F0, specF, N, V, L);
    vec3 f_r = data.diffuse + data.specular;

    return max(vec3(0.0f), f_r * radiance * NoL);
}

vec3 PointDiffuseLighting(
    vec3 albedo, float roughness, float metallic,
    vec4 worldPos, vec3 N, vec3 V, uint lightID)
{
	if(lightID == LIGHT_ID_INVALID) return vec3(0.0f);	

    LightStructGPU pointLight = LIGHTS.slot[lightID];

    float pointDistance = length(worldPos.xyz - pointLight.position_range.xyz);

    vec3 L = normalize(pointLight.position_range.xyz - worldPos.xyz);
    float NoL = saturate(dot(N, L));

    float pShadow = 1.0f;
    //float pShadow       = PointShadow(lightID, worldPos);  
    float attenuation   = PointLightFalloff(pointDistance, pointLight.position_range.w);

    vec3 radiance = pShadow *               //辐射率 = 阴影值 * 光强 * 颜色 * 衰减
                    pointLight.radiance_type.rgb *
                    attenuation;   

    vec3 f_r = ResolveBRDF(albedo, roughness, metallic, N, V, L).diffuse;

    return max(vec3(0.0f), f_r * radiance * NoL);	
}


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
