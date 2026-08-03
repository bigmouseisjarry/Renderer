#ifndef SCREEN_GLSL
#define SCREEN_GLSL

vec2 ScreenPixToUV(ivec2 pixel, ivec2 totalPixels)  // 左上角[0, 0]，右下角[1, 1]，像素中心UV实测需要偏移0.5
{
    return (pixel + vec2(0.5f)) / vec2(totalPixels);
}

vec2 ScreenPixToUV(vec2 pixel, ivec2 totalPixels)  // 左上角[0, 0]，右下角[1, 1]，像素中心UV实测需要偏移0.5
{
    return (pixel + vec2(0.5f)) / vec2(totalPixels);
}

ivec2 UVToNearestScreenPix(vec2 uv, ivec2 totalPixels)  // 取离对应UV最近的整像素坐标
{
    vec2 pixel = uv * totalPixels - vec2(0.5f);
    pixel = floor(pixel) + ceil(fract(pixel));    // 四舍五入

    return ivec2(pixel);
}

ivec2 UVToNearest2x2ScreenPix(vec2 uv, ivec2 totalPixels)   // 取离对应UV最近的2x2整像素坐标的左上角
{
    vec2 pixel = uv * totalPixels - vec2(0.5f);
    pixel = floor(pixel);

    return ivec2(pixel);
}

vec4 UVToNearest2x2ScreenPixWeights(vec2 uv, ivec2 totalPixels)   // 2x2像素的双线性插值权重
{
    vec2 pixel = uv * totalPixels - vec2(0.5f);
    pixel = fract(pixel);

    return vec4((1.0 - pixel.x) * (1.0 - pixel.y),
                pixel.x         * (1.0 - pixel.y),
                (1.0 - pixel.x) * pixel.y,
                pixel.x         * pixel.y);
}

#ifdef COMMON_GLSL

vec2 ScreenPixToUV(ivec2 pixel) 
{
    return ScreenPixToUV(pixel, ivec2(WINDOW_WIDTH, WINDOW_HEIGHT));
}

vec2 ScreenPixToUV(vec2 pixel) 
{
    return ScreenPixToUV(pixel, ivec2(WINDOW_WIDTH, WINDOW_HEIGHT));
}

ivec2 UVToNearestScreenPix(vec2 uv)
{
    return UVToNearestScreenPix(uv, ivec2(WINDOW_WIDTH, WINDOW_HEIGHT));
}

ivec2 UVToNearest2x2ScreenPix(vec2 uv)
{
    return UVToNearest2x2ScreenPix(uv, ivec2(WINDOW_WIDTH, WINDOW_HEIGHT));
}

vec4 UVToNearest2x2ScreenPixWeights(vec2 uv)
{
    return UVToNearest2x2ScreenPixWeights(uv, ivec2(WINDOW_WIDTH, WINDOW_HEIGHT));
}

vec2 HalfScreenPixToUV(ivec2 pixel) 
{
    return ScreenPixToUV(pixel, ivec2(HALF_WINDOW_WIDTH, HALF_WINDOW_HEIGHT));
}

vec2 HalfScreenPixToUV(vec2 pixel) 
{
    return ScreenPixToUV(pixel, ivec2(HALF_WINDOW_WIDTH, HALF_WINDOW_HEIGHT));
}

ivec2 UVToNearestHalfScreenPix(vec2 uv)
{
    return UVToNearestScreenPix(uv, ivec2(HALF_WINDOW_WIDTH, HALF_WINDOW_HEIGHT));
}

vec2 FetchScreenPixPos(vec2 coord)
{
	return vec2(coord.x * WINDOW_WIDTH, coord.y * WINDOW_HEIGHT);
}

vec2 FetchScreenPixPos(vec3 ndc)
{
    return FetchScreenPixPos(NDCToScreen(ndc.xy));
}

vec2 FetchHalfScreenPixPos(vec2 coord)
{
	return vec2(coord.x * HALF_WINDOW_WIDTH, coord.y * HALF_WINDOW_HEIGHT);
}

vec2 FetchHalfScreenPixPos(vec3 ndc)
{
    return FetchHalfScreenPixPos(NDCToScreen(ndc.xy));
}

#endif

#endif