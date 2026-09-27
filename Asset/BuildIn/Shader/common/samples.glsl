#ifndef SAMPLES_GLSL
#define SAMPLES_GLSL

#define GOLDEN_ANGLE 2.39996323

// Vogel 圆盘
vec2 VogeLDisk(float i, float rotation, float sampleCountSqrt) 
{
    float r = sqrt(i + 0.5) / sampleCountSqrt;
    float theta = i * GOLDEN_ANGLE + rotation;
    return vec2(cos(theta), sin(theta)) * r;
}

#endif