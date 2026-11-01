#ifndef RAY_QUERY_GLSL
#define RAY_QUERY_GLSL

#if(ENABLE_RAY_TRACING != 0)

// instance mask位规划（与RHI侧RHIAccelerationStructureInstanceMaskBits一致，TLAS构建侧按材质填充；
// Ray Mask Culling语义=instance.mask & cullMask == 0则丢弃交点）
#define INSTANCE_MASK_SCENE          (1u << 0)      // bit0=场景/GI可见
#define INSTANCE_MASK_SHADOW_CASTER  (1u << 1)      // bit1=阴影投射体（材质CastShadow）

bool RayQueryVisibility(vec3 from, vec3 to, float minDist, uint cullMask)
{
	float tmin = MIN_RAY_TRACING_DISTANCE;
    //float tmax = MAX_RAY_TRACING_DISTANCE;
	float tmax = max(length(to - from) - minDist - 1e-3, tmin);
	vec3 dir = normalize(to - from);

    rayQueryEXT query;
    rayQueryInitializeEXT(
        query,
        TLAS,
        gl_RayFlagsOpaqueEXT,  // gl_RayFlagsCullBackFacingTrianglesEXT gl_RayFlagsTerminateOnFirstHitEXT?
        cullMask,
        from + dir * minDist,
        tmin,
        dir,
        tmax);

    rayQueryProceedEXT(query);

    float dist = tmax;
    if (rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT)
    {
        //dist = rayQueryGetIntersectionTEXT(query, true);
        return true;    //命中物体
    }

    // return dist < tmax;
    return false;
}

// 无mask版本（兼容存量调用）：全可见
bool RayQueryVisibility(vec3 from, vec3 to, float minDist)
{
    return RayQueryVisibility(from, to, minDist, 0xFF);
}

bool RayQueryHitObject(vec3 from, vec3 to, float minDist,
    out uint objectID, out vec4 worldPos, out vec3 worldNormal) 
{
    objectID = 0;
    worldPos = vec4(0.0f);
    worldNormal = vec3(0.0f);

	float tmin = MIN_RAY_TRACING_DISTANCE;
    //float tmax = MAX_RAY_TRACING_DISTANCE;  
	float tmax = max(length(to - from) - minDist - 1e-3, tmin);
	vec3 dir = normalize(to - from);

    rayQueryEXT query;
    rayQueryInitializeEXT(
        query, 
        TLAS, 
        gl_RayFlagsOpaqueEXT, // gl_RayFlagsCullBackFacingTrianglesEXT gl_RayFlagsTerminateOnFirstHitEXT?
        0xFF, 
        from + dir * minDist, 
        tmin, 
        dir, 
        tmax);

    // 循环形态只在要做 alpha 测试这类逐候选决策时才需要
    rayQueryProceedEXT(query);

    if (rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT)
    {
        float dist          = rayQueryGetIntersectionTEXT(query, true);
        vec2 hitAttribute   = rayQueryGetIntersectionBarycentricsEXT(query, true);
        objectID            = rayQueryGetIntersectionInstanceCustomIndexEXT(query, true);
        uint triangleID    	= rayQueryGetIntersectionPrimitiveIndexEXT(query, true);
        vec3 barycentrics   = vec3(1.0f - hitAttribute.x - hitAttribute.y, hitAttribute.x, hitAttribute.y);

        mat4 model          = FetchModel(objectID);
        uvec3 index         = FetchTriangleIndex(objectID, triangleID, barycentrics);
        vec4 pos            = FetchTrianglePos(objectID, index, barycentrics);
        worldNormal         = FetchWorldNormal(FetchTriangleNormal(objectID, index, barycentrics), model);
        // worldPos            = model * pos;   // 不太准

        worldPos.xyz        = from + dir * (minDist + dist);
        worldPos.w          = 1.0f;

        return true;
    }

    return false;
}

#endif

#endif