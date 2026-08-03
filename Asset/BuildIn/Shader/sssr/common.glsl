
layout(push_constant) uniform SSSRSetting {
    int mode;
    int maxMip;
    int startMip;
    int endMip;
    int maxLoop;

    float maxRoughness;
    float minHitDistance;
    float importanceSampleBias;
    float thickness;
    float screenFade;
    float filterBlend;

    int enableSkybox;		
	int reflectionOnly;
    int visualizeHitUV;
    int visualizeHitMask;

} SETTING;

layout(set = 1, binding = 0, rgba16f)   uniform image2D SSSR_HIT_RESULT;	    // trace到的命中信息
layout(set = 1, binding = 1, rgba16f)   uniform image2D SSSR_HIT_COLOR;	        // trace到的radiance
layout(set = 1, binding = 2, rgba16f)   uniform image2D SSSR_PDF;	            // trace到的pdf，后三位暂时没用（DEBUG）
layout(set = 1, binding = 3, rgba16f)   uniform image2D SSSR_RESOLVE;	        // resolve结果
layout(set = 1, binding = 4, rgba16f)   uniform image2D SSSR_HISTORY;		    // 历史帧
layout(set = 1, binding = 5, rgba16f)   uniform image2D OUT_COLOR;		        // 混合历史帧最后的输出结果	
layout(set = 1, binding = 6)            uniform texture2D IN_COLOR_PYRAMID;     // 输入的屏幕纹理，带完整mip
layout(set = 1, binding = 7)            uniform texture2D BRDF_LUT;	            // split sum的预积分纹理
layout(set = 1, binding = 8)            uniform texture2D REPROJECTION_RESULT;	// 重投影信息

layout(set = 2, binding = 0, rgba8)         uniform image2D G_BUFFER_DIFFUSE_METALLIC;	// G-Buffer
layout(set = 2, binding = 1, rgba8_snorm)   uniform image2D G_BUFFER_NORMAL_ROUGHNESS;	
layout(set = 2, binding = 2, rgba16f)       uniform image2D G_BUFFER_EMISSION;	