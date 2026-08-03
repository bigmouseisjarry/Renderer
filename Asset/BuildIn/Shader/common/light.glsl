#ifndef LIGHT_GLSL
#define LIGHT_GLSL

// #define SHADOW_QUALITY_LOW    // 使用低质量阴影时定义

// cluster lighting 辅助函数////////////////////////////////////////////////////////////////////////////

ivec3 FetchLightClusterGrid(vec3 ndc)
{
    vec2 screenPixPos   = FetchScreenPixPos(ndc);

    float depth         = ndc.z;   
    float linearDepth   = LinearEyeDepth(depth, CAMERA.near, CAMERA.far);

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
float TextureProj(vec4 ndcPos, vec2 offset, texture2D tex)
{
	float shadow = 1.0;
	if ( ndcPos.z > -1.0 && ndcPos.z < 1.0 && ndcPos.x > -0.99 && ndcPos.x < 0.99 && ndcPos.y > -0.99 && ndcPos.y < 0.99) 
	{
		float dist = texture(sampler2D(tex, SAMPLER[0]), (ndcPos.xy + vec2(1.0)) * 0.5 + offset).r;
		if ( ndcPos.z > 0.0 && dist + 0.0002 < ndcPos.z ) 	//比较的时候还得加个小bias质量更好
		{
			shadow = 0;
		}
	}
	return shadow;
}

// PCF过滤 NDC空间坐标，纹理尺寸，纹理，层级
float FilterPCF(vec4 ndcPos, ivec2 texSize, texture2D tex)
{
	float scale = 1.0;
	float dx = scale * 1.0 / float(texSize.x);
	float dy = scale * 1.0 / float(texSize.y);

	float shadowFactor = 0.0;
	int count = 0;
	int range = 1;	//非常影响性能，就滤波9个点吧
	
	for (int x = -range; x <= range; x++)
	{
		for (int y = -range; y <= range; y++)
		{
			shadowFactor += TextureProj(ndcPos, vec2(dx*x, dy*y), tex);
			count++;
		}
	}
	// return shadowFactor / count;
	return smoothstep(0, 1, shadowFactor / count);
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

float VSM(vec2 moments, float depth, float c1)
{
	float x = exp(c1 * depth);
	float variance_x = max(moments.y - pow(moments.x, 2), 0.0001);
	float d_x = x - moments.x;  

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

// 获取平行光源阴影值
float DirectionalShadow(vec4 worldPos)
{
	int cascadeIndex = CascadeLevel(worldPos);

    DirectionalLight dirLight = LIGHTS.directionalLights[cascadeIndex];
	if(!(dirLight.castShadow > 0)) return 1.0f;	// 不投射阴影

    vec3 dirLightVec = -normalize(dirLight.dir);    

    vec4 ndcPos = dirLight.proj * dirLight.view * worldPos;	
    ndcPos /= ndcPos.w;

#ifdef SHADOW_QUALITY_LOW

    float dirShadow = 1.0f;	
	float dist = texture(sampler2D(DIRECTIONAL_SHADOW[cascadeIndex], SAMPLER[0]), (ndcPos.xy + vec2(1.0)) * 0.5).r;
	if ( ndcPos.z > 0.0 && dist + 0.0002 < ndcPos.z ) 	//比较的时候还得加个小bias质量更好
	{
		dirShadow = 0.0f;
	}
	return dirShadow;

#else

	ivec2 texSize = textureSize(DIRECTIONAL_SHADOW[cascadeIndex], 0).xy;

    float dirShadow = FilterPCF(ndcPos, texSize, DIRECTIONAL_SHADOW[cascadeIndex]);	//计算平行光源阴影取值，[0,1]
	dirShadow = smoothstep(0.0f, 1.0f, min(1.0, dirShadow / 0.9));	                            //重映射

	return dirShadow;

#endif
}

// 获取点光源阴影值
float PointShadow(uint lightID, vec4 worldPos)
{
	if(lightID == 0) return 0.0f;	// 0为无效ID

	PointLight pointLight = LIGHTS.pointLights[lightID];
	if(pointLight.shadowID >= MAX_POINT_SHADOW_COUNT) return 1.0f;	//无效阴影索引

	vec3 pointLightVec = normalize(pointLight.pos - worldPos.xyz);
	float pointDistance = length(worldPos.xyz - pointLight.pos);

#ifdef SHADOW_QUALITY_LOW	// 直接用深度

	float pointShadow =  pointDistance / pointLight.far - texture(samplerCube(POINT_SHADOW[pointLight.shadowID], SAMPLER[0]), -pointLightVec).r;
	pointShadow = pointShadow > pointLight.bias ? 0.0f : 1.0f;	//给阴影一点颜色？

#else	// 使用VSM

	vec2 moments = texture(samplerCube(POINT_SHADOW[pointLight.shadowID], SAMPLER[0]), -pointLightVec).xy;

	float depth = pointDistance / pointLight.far;

    float pointShadow = VSM(moments, depth, pointLight.c1);
	//float pointShadow = EVSM(moments, depth, pointLight.c1, pointLight.c2);		//计算点光源阴影取值，[0,1]
	pointShadow = smoothstep(0.0f, 1.0f, min(1.0, pointShadow / 0.8));	            //重映射，去掉大于0.8的部分

#endif

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
        float NoL = saturate(dot(N, L));

        float dirShadow     = 1.0f;
		//float dirShadow = DirectionalShadow(worldPos);               

        vec3 radiance = dirShadow *           //辐射率 = 阴影值 * 光强 * 颜色
                        dirLight.color * 
                        dirLight.intencity;        

        BRDFData data = ResolveBRDF(albedo.xyz, roughness, metallic, N, V, L);
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

        vec3 f_r = ResolveBRDF(albedo.xyz, roughness, metallic, N, V, L).diffuse;

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

    BRDFData data = ResolveBRDF(albedo.xyz, roughness, metallic, N, V, L);
    vec3 f_r = data.diffuse + data.specular;

    return max(vec3(0.0f), f_r * radiance * NoL);	
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

    vec3 f_r = ResolveBRDF(albedo.xyz, roughness, metallic, N, V, L).diffuse;

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

vec3 IndirectIrradiance(
    vec3 albedo, float roughness, float metallic,
    vec4 worldPos, vec3 N, vec3 V)
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
        vec3 indirectIrradiance = IndirectIrradiance(albedo, roughness, metallic, worldPos, N, V);

        float NoV = saturate(dot(N, V));

        vec3 k_specular = FresnelSchlickRoughness(NoV, F0, roughness); 
        vec3 k_diffuse  = 1.0 - k_specular;     //BRDF积分项里每个radiance对应的kD是不同的（k_specular = F 视角相关），把kD移出积分是一个近似估计

        vec3 f_diffuse  = Diffuse_Lambert(albedo);  

        vec3 diffuseColor = (1.0 - metallic) * k_diffuse * f_diffuse * indirectIrradiance;      //  ?
        // vec3 diffuseColor = albedo / PI * indirectIrradiance;                                // 所有表面都当完美漫反射表面计算

        indirectColor = diffuseColor;
    }

    //环境光照计算，已经在SSSR里完成了
    /*
    vec3 ambientColor = vec3(0.0);
    {
        float NoV = clamp(dot(N, V), 0.00001, 0.99999);

        vec3 kS = FresnelSchlickRoughness(NoV, F0, roughness); 
        vec3 kD = 1.0 - kS;

        //漫反射IBL
        vec3 irradiance = texture(DIFFUSE_IBL_SAMPLER, N).rgb;
        vec3 diffuse    = irradiance * albedo.rgb;
        vec3 diffuseColor = kD * diffuse; //漫反射环境光照得到的颜色

        //镜面反射IBL
        vec2 brdf = texture(BRDF_LUT, vec2(NoV, roughness)).rg;
        brdf = pow(brdf, vec2(1.0/2.2));      //gamma矫正
        vec3 reflection = PreFilteredReflection(R, roughness, SPECULAR_IBL_SAMPLER).rgb;	
	    vec3 specularColor = reflection * (kS * brdf.x + brdf.y);

        ambientColor = diffuseColor + specularColor;
    }

    //对于大场景的IBL不对，暂时先用定值
    {
        float NoV = clamp(dot(N, V), 0.00001, 0.99999);

        vec3 kS = FresnelSchlickRoughness(NoV, F0, roughness); 
        vec3 kD = 1.0 - kS;

        //漫反射IBL
        vec3 irradiance  = vec3(0.2);
        vec3 diffuse = irradiance * albedo.rgb;
        vec3 diffuseColor = kD * diffuse;    //漫反射环境光照得到的颜色

        ambientColor = diffuseColor * ao;
    }
    */

    return indirectColor;
}

#endif