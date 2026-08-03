#ifndef DDGI_LAYOUT_GLSL
#define DDGI_LAYOUT_GLSL

layout(set = 1, binding = 0, rgba8)         uniform image2D G_BUFFER_DIFFUSE_METALLIC;	    //漫反射纹理
layout(set = 1, binding = 1, rgba8_snorm)   uniform image2D G_BUFFER_NORMAL_ROUGHNESS;	    //法线纹理
layout(set = 1, binding = 2, rgba16f)       uniform image2D G_BUFFER_EMISSION;	            //自发光纹理	
layout(set = 1, binding = 3, rgba16f)       uniform image2D G_BUFFER_POSITION;	            //世界坐标纹理

layout(set = 1, binding = 4, rgba16f)       uniform image2D DDGI_RADIANCE;	                //辐射率纹理
layout(set = 1, binding = 5, rgba16f)       uniform image2D DDGI_IRRADIANCE;	            //辐照度纹理
layout(set = 1, binding = 6, rg16f)         uniform image2D DDGI_DEPTH;		                //深度纹理

#endif