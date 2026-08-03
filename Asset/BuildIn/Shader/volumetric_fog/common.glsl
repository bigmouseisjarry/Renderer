ivec3 FetchVolumetricClusterID(vec3 ndc)
{
    vec2 coord = NDCToScreen(ndc.xy) + 0.5 * vec2(1.0 / WINDOW_WIDTH, 1.0 / WINDOW_WIDTH);	//需要加上一点偏移

    float depth         = ndc.z;   
    float linearDepth   = LinearEyeDepth(depth, CAMERA.near, CAMERA.far);

    uint clusterZ       = uint(VOLUMETRIC_FOG_SIZE_Z * log(linearDepth / CAMERA.near) / log(CAMERA.far / CAMERA.near));  //对数划分
    //uint clusterZ     = uint( (linearDepth - CAMERA.near) / ((CAMERA.far - CAMERA.near) / VOLUMETRIC_FOG_SIZE_Z));   //均匀划分
    ivec3 clusterID     = ivec3(uint(coord.x * VOLUMETRIC_FOG_SIZE_X), uint(coord.y * VOLUMETRIC_FOG_SIZE_Y), clusterZ);

    return clusterID;
}

vec4 FetchVolumetricClusterPos(vec3 clusterID)
{
	vec3 voxelSize 			= 1.0f / vec3(VOLUMETRIC_FOG_SIZE_X, VOLUMETRIC_FOG_SIZE_Y, VOLUMETRIC_FOG_SIZE_Z);
	
	vec2 screenPos 			= clusterID.xy * voxelSize.xy;	
	vec4 viewFarPos 		= ScreenToView(screenPos, 1.0f);
	vec4 viewNearPos 		= ScreenToView(screenPos, 0.0f);

	//均匀划分
	// vec4 worldNearPos 	= vec4(CAMERA.pos.xyz + normalize(CAMERA.front) * CAMERA.near, 1.0f);	//世界空间最近处
	// vec4 worldFarPos 	= screenToWorld(screenPos, 1.0f);					//世界空间最远处
	// vec4 worldPos 		= Lerp(worldNearPos, worldFarPos, (gID.z + ditter.z) * voxelSize.z);

	// 对数划分，使用线面相交计算
	// vec4 plane;
	// plane.xyz 			= vec3(0.0f, 0.0f, -1.0f);  
	// plane.w 				= CAMERA.near * pow(CAMERA.far / CAMERA.near, (gID.z + ditter.z) * voxelSize.z);	//对数划分

	// vec3 viewPos;
	// vec3 eye 			= vec3(0, 0, 0);
	// LineIntersectPlane(eye, viewFarPos.xyz, plane, viewPos);

	// 对数划分,也可以这样算
	float viewDepth 		= CAMERA.near * pow((CAMERA.far / CAMERA.near), clusterID.z * voxelSize.z);	
	vec3 viewPos 			= (viewFarPos * (viewDepth / abs(viewFarPos.z))).xyz;

	return ViewToWorld(vec4(viewPos, 1.0f));	
}

layout(set = 1, binding = 0, rgba16f) uniform image3D VOLUMETRIC_FOG_RAW;				//未计算积分时的体素纹理
layout(set = 1, binding = 1, rgba16f) uniform image3D VOLUMETRIC_FOG_RAW_HISTORY;		//未计算积分时的历史帧
layout(set = 1, binding = 2, rgba16f) uniform image3D VOLUMETRIC_FOG_INTEGRAL;			//积分体素纹理
layout(set = 1, binding = 3, rgba16f) uniform image2D VOLUMETRIC_FOG_IMG;				//输出纹理

layout(push_constant) uniform VolumetricFogSetting {
	float henyeyGreensteinG;
	float attenuationFactor;	// 衰减系数 = 外散射系数 + 吸收率
	float scatteringIntencity;
	float blendFactor;
	float indirectLightIntencity;
	uint isotropic;
	uint fogOnly;
} SETTING;                    