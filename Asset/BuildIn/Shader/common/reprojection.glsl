#ifndef REPROJECTION_GLSL
#define REPROJECTION_GLSL

// 计算速度 ////////////////////////////////////////////////////////////////////
vec2 CalculateVelocity(vec4 pos, vec4 prevPos)
{
    vec4 clipPos            = CAMERA.viewProj * pos;
    clipPos                 /= clipPos.w;
    vec2 coordPos           = clipPos.xy * 0.5 + vec2(0.5);

    vec4 prevClipPos        = CAMERA.prevViewProj * prevPos;
    prevClipPos             /= prevClipPos.w;
    vec2 prevCoordPos       = prevClipPos.xy * 0.5 + vec2(0.5);

    vec2 velocity           = vec2(prevCoordPos.xy - coordPos.xy);

    //vec4 velocity         = vec4(vec3(clipPos.xyz - prevClipPos.xyz), 0.0f);
    //velocity.xy          -= (CAMERA.prev_jitter.xy - CAMERA.jitter.xy);
    //velocity.z          = 0.0f;

    //速度坐标方向：x正方向 y负方向

    //velocity = vec4(coordPos, 0.0f, 0.0f);

    // if(CAMERA.view == CAMERA.prevView)  // 没动的情况下强制置零，避免精度误差
    //     velocity = vec2(0.0f);

    return velocity;
}

// 重投影 ////////////////////////////////////////////////////////////////////
// https://github.com/flwmxd/LuxGI

#define NORMAL_DISTANCE 0.1f
#define PLANE_DISTANCE 1.0f

bool CheckOutOfFrame(ivec2 pixel, ivec2 textureSize)
{
   return any(lessThan(pixel, ivec2(0, 0))) || any(greaterThan(pixel, textureSize - ivec2(1, 1)));  
}

bool CheckNormal(vec3 normal, vec3 prevNormal)
{
    return pow(abs(dot(normal, prevNormal)), 2) <= NORMAL_DISTANCE; 
}

bool CheckPlaneDistance(vec3 pos, vec3 prevPos, vec3 normal)
{
    vec3  toVec    = pos - prevPos;
    float distance = abs(dot(toVec, normal));   
    return distance > PLANE_DISTANCE;           
}

bool CheckObjectId(uint objectID, uint prevObjectID)
{
    return objectID != prevObjectID;    
}

bool HistoryIsValid(ivec2 pixel, ivec2 textureSize,
                    vec3 currentPos, vec3 prevPos, 
                    vec3 normal, vec3 prevNormal, 
                    uint objectID, uint prevObjectID)
{
    if (CheckOutOfFrame(pixel, textureSize))                return false;   // 坐标超出屏幕范围 
    if (CheckObjectId(objectID, prevObjectID))              return false;   // 法线差异过大
    if (CheckPlaneDistance(currentPos, prevPos, normal))    return false;   // 前帧位置距离当前位置所在平面距离过大
    if (CheckNormal(normal, prevNormal))                    return false;   // 不是同一个物体

    return true;
}

bool Reproject(in ivec2 pixel, 
               in vec2 motion,
               in uint objectID,
               in vec3 normal,
               in utexture2D prevObjectIdTex,
               in texture2D prevNormalTex,
			   in texture2D prevColorTex,
			   in texture2D prevMomentsTex,
               out vec3 historyColor, 
               out vec2 historyMoments, 
               out float historyLength)
{
	historyColor = vec3(0.0f);
    historyMoments = vec2(0.0f);
	historyLength = 0.0f;

    ivec2 textureSize   = ivec2(textureSize(prevObjectIdTex, 0));
    
    vec2 uv             = ScreenPixToUV(pixel); 
    ivec2 nearestPixel  = UVToNearestScreenPix(uv, textureSize);

    vec3 worldPos       = DepthToWorld(uv).xyz;

    vec2 historyUV    			= uv + motion;
    ivec2 historyPixelNear  	= UVToNearestScreenPix(historyUV);  
    ivec2 historyPixelFloor 	= UVToNearest2x2ScreenPix(historyUV);
	vec4 historyPixelWeights 	= UVToNearest2x2ScreenPixWeights(historyUV);
    
	bool valid = false;
    ivec2 offset[4] = { 
        ivec2(0, 0), 
        ivec2(1, 0),
        ivec2(0, 1),
        ivec2(1, 1) 
    };
    bool neighborValid[4];
    for (int i = 0; i < 4; i++)   
    {
        ivec2 neighborPixel = historyPixelFloor + offset[i];
        vec2 neighborUV     = ScreenPixToUV(neighborPixel);

        vec3 prevNormal     = texelFetch(prevNormalTex, neighborPixel, 0).xyz;
        vec3 prevPos        = PrevDepthToWorld(neighborUV).xyz;  
        uint prevObjectID   = texelFetch(prevObjectIdTex, neighborPixel, 0).r;

        neighborValid[i]    = HistoryIsValid(
                                neighborPixel, textureSize,
                                worldPos, prevPos, 
                                normal, prevNormal, 
                                objectID, prevObjectID);

        valid = valid || neighborValid[i];
    }

	if(valid) // 对有效的2x2临近像素做双线性插值
	{
		float sumWeights = 0.0f;
		for (int i = 0; i < 4; i++)   
		{
			if(neighborValid[i])
			{
				ivec2 neighborPixel = historyPixelFloor + offset[i];
				vec2 neighborUV     = ScreenPixToUV(neighborPixel);

				historyColor   	+= historyPixelWeights[i] * texelFetch(prevColorTex, neighborPixel, 0).xyz;
				historyMoments 	+= historyPixelWeights[i] * texelFetch(prevMomentsTex, neighborPixel, 0).xy;
				historyLength  	+= historyPixelWeights[i] * texelFetch(prevMomentsTex, neighborPixel, 0).z;
				sumWeights 		+= historyPixelWeights[i];
			}
		}

		valid        	= (sumWeights >= 0.01);
        historyColor   	= valid ? historyColor / sumWeights : vec3(0.0f);
        historyMoments 	= valid ? historyMoments / sumWeights : vec2(0.0f);
		historyLength 	= valid ? historyLength / sumWeights : 0.0f;
	}

	return valid;
}


void Reproject(in ivec2 pixel, 
               in vec2 motion,
               in uint objectID,
               in vec3 normal,
               in utexture2D prevObjectIdTex,
               in texture2D prevNormalTex,
               out vec2 historyUV,
               out ivec2 historyPixel)
{
    ivec2 textureSize   = ivec2(textureSize(prevObjectIdTex, 0));
    bool valid          = false;

    vec2 uv             = ScreenPixToUV(pixel); 
    ivec2 nearestPixel  = UVToNearestScreenPix(uv, textureSize);

    vec3 worldPos       = DepthToWorld(uv).xyz;

    historyUV           = uv + motion;
    historyPixel        = UVToNearestScreenPix(historyUV);
    ivec2 historyPixelNear  = historyPixel;  
    ivec2 historyPixelFloor = UVToNearest2x2ScreenPix(historyUV);
    
    ivec2 offset[4] = { 
        ivec2(0, 0), 
        ivec2(1, 0),
        ivec2(0, 1),
        ivec2(1, 1) 
    };
    float distance = 1000.0f;
    for (int i = 0; i < 4; i++)   
    {
        ivec2 neighborPixel = historyPixelFloor + offset[i];
        vec2 neighborUV     = ScreenPixToUV(neighborPixel);

        vec3 prevNormal     = texelFetch(prevNormalTex, neighborPixel, 0).xyz;
        vec3 prevPos        = PrevDepthToWorld(neighborUV).xyz;  
        uint prevObjectID   = texelFetch(prevObjectIdTex, neighborPixel, 0).r;

        bool pixelValid     = HistoryIsValid(
                                neighborPixel, textureSize,
                                worldPos, prevPos, 
                                normal, prevNormal, 
                                objectID, prevObjectID);

        if(pixelValid && length(neighborUV - historyUV) < distance)
        {
            distance = length(neighborUV - historyUV);
            historyPixel = neighborPixel;
        }

        valid = valid || pixelValid;
    }

    if (!valid) 
    {
        historyUV    = vec2(-1.0f);
        historyPixel = ivec2(-1);
    }
}

#endif