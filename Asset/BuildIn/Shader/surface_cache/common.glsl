
struct SurfaceCacheLightingDispatch
{
    uint meshCardID;
    uint objectID;
    uvec2 tileOffset;  
    uvec2 tileIndex;  
}; 

layout(set = 1, binding = 0)            uniform texture2D CACHE_DIFFUSE_METALLIC;	    
layout(set = 1, binding = 1)            uniform texture2D CACHE_NORMAL_ROUGHNESS;	    
layout(set = 1, binding = 2)            uniform texture2D CACHE_EMISSION;	            
layout(set = 1, binding = 3, rgba16f)   uniform image2D CACHE_LIGHTING;	           
layout(set = 1, binding = 4)            uniform texture2D CACHE_DEPTH;	

layout(set = 1, binding = 5) buffer direct_lighting_dispatch { 

    SurfaceCacheLightingDispatch slot[];

} CACHE_DIRECT_LIGHTING_DISPATCH;