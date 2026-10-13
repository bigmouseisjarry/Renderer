#ifndef DDGI_LAYOUT_GLSL
#define DDGI_LAYOUT_GLSL

layout(set = 1, binding = 0, rgba8)         uniform image2D TRACE_ALBEDO_METALLIC;	        //rgb=原始反照率，a=metallic
layout(set = 1, binding = 1, rgba8_snorm)   uniform image2D TRACE_NORMAL_ROUGHNESS;	    //rgb=世界法线，a=roughness
layout(set = 1, binding = 2, rgba16f)       uniform image2D TRACE_EMISSION;	                //线性自发光
layout(set = 1, binding = 3, rgba16f)       uniform image2D TRACE_DIRECTION_DIST;	        //rgb=光线方向，a=命中距离

layout(set = 1, binding = 4, rgba16f)       uniform image2D DDGI_RADIANCE;	                //辐射率纹理
layout(set = 1, binding = 5, rgba16f)       uniform image2D DDGI_IRRADIANCE;	            //辐照度纹理
layout(set = 1, binding = 6, rg16f)         uniform image2D DDGI_DEPTH;		                //深度纹理

#endif