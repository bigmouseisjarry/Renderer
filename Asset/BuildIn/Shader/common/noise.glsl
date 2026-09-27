#ifndef NOISE_GLSL
#define NOISE_GLSL

float IGNRotation(ivec2 pixel)
{
    return fract(52.9829189 * fract(0.06711056 * pixel.x + 0.00583715 * pixel.y)) * 6.28318530 + GLOBAL_SETTING.totalTicks * GOLDEN_ANGLE;
}

float StbnRotation(ivec2 pixel)
{
    return texture(sampler2D(STBN[GLOBAL_SETTING.totalTicks % 64u], SAMPLER[4]),vec2(pixel) / 128.0).r * 6.2831853;
}

#endif