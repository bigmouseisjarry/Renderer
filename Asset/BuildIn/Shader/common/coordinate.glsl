#ifndef COORDINATE_GLSL
#define COORDINATE_GLSL

//坐标转换////////////////////////////////////////////////////////////////////////////

//坐标系
//world space:      x正向向前，y正向向上，z正向向右
//view space:       x正向向右，y正向向上，z负向向前
//clip space:       x正向向右，y负向向上，z正向向前
//NDC space;        左上[-1, -1]，右下[1, 1]（透视除法后）
//screen space:     左上[0, 0]，右下[1, 1]

vec2 NDCToScreen(in vec2 ndc)
{
    return ndc.xy * 0.5 + 0.5;
}

vec2 ScreenToNDC(in vec2 screen)
{
    return screen.xy * 2.0 - 1.0;
}

vec4 NDCToView(in vec4 ndc)
{	
	vec4 view = CAMERA.invProj * ndc;       // View space position.
	view /= view.w;                         // Perspective projection.

	return view;
}

vec4 ViewToClip(in vec4 view)
{
    vec4 clip = CAMERA.proj * view;

    return clip;
}

vec4 ViewToNDC(in vec4 view)
{
    vec4 ndc = CAMERA.proj * view;
    ndc /= ndc.w;

    return ndc;
}  

vec2 ViewToScreen(in vec4 view)
{
    return NDCToScreen(ViewToNDC(view).xy);
}  

vec4 ViewToWorld(in vec4 view)
{
    return CAMERA.invView * view;
}  

vec4 WorldToView(in vec4 world)
{
    return CAMERA.view * world;
}  

vec4 WorldToClip(in vec4 world)
{
    return CAMERA.viewProj * world;
}

vec4 WorldToNDC(in vec4 world)
{
    vec4 ndc = CAMERA.viewProj * world;
    ndc /= ndc.w;

    return ndc;
}  

vec2 WorldToScreen(in vec4 world)
{
    return NDCToScreen(WorldToNDC(world).xy);
}

vec4 NDCToWorld(in vec4 ndc)
{	
	return ViewToWorld(NDCToView(ndc));
}

vec4 ScreenToView(in vec2 coord, in float depth)
{
    vec4 ndc = vec4(ScreenToNDC(coord), depth, 1.0f);  
	return NDCToView(ndc);
}

vec4 ScreenToWorld(in vec2 coord, in float depth)
{
    return ViewToWorld(ScreenToView(coord, depth));
}

vec4 DepthToView(in vec2 coord)
{
    return ScreenToView(coord, FetchDepth(coord));
}

vec4 PrevDepthToView(in vec2 coord)
{
    return ScreenToView(coord, FetchPrevDepth(coord));
}

vec4 DepthToWorld(in vec2 coord)
{
    return ViewToWorld(DepthToView(coord));
}   

vec4 DepthToWorld(in vec2 coord, in float depth)
{
    return ViewToWorld(ScreenToView(coord, depth));
}   

vec4 PrevDepthToWorld(in vec2 coord)
{
    return ViewToWorld(PrevDepthToView(coord));
}  

#endif