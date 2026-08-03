#pragma once

#include "Function/Global/Definations.h"
#include "Function/Render/RenderResource/Shader.h"
#include "Function/Render/RenderResource/Sampler.h"
#include "Function/Render/RenderResource/Buffer.h"
#include "Function/Render/RenderResource/Material.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RDG/RDGBuilder.h"

#include <string>
#include <cstdint>

enum PassType	
{  
    GPU_CULLING_PASS = 0,
	CLUSTER_LIGHTING_PASS,
	IBL_PASS,
	DEPTH_PASS,
	DEPTH_PYRAMID_PASS,
	POINT_SHADOW_PASS,
    DIRECTIONAL_SHADOW_PASS,
	CLIPMAP_PASS,
	DDGI_PASS,
	G_BUFFER_PASS,
	SURFACE_CACHE_PASS,
	REPROJECTION_PASS,
    DEFERRED_LIGHTING_PASS,
	RESTIR_DI_PASS,
	RESTIR_GI_PASS,
	SVGF_PASS,
	SSSR_PASS,
	NRD_PASS,
	VOLUMETIRC_FOG_PASS,
	FORWARD_PASS,
    TRANSPARENT_PASS,
	CLIPMAP_VISUALIZE_PASS,
	DDGI_VISUALIZE_PASS,
	PATH_TRACING_PASS,
	RAY_TRACING_BASE_PASS,
	BLOOM_PASS,
	FXAA_PASS,
	TAA_PASS,
	EXPOSURE_PASS,
	POST_PROCESSING_PASS,
	GIZMO_PASS,
	TEST_TRIANGLE_PASS,
    EDITOR_UI_PASS,
    PRESENT_PASS,

	PASS_TYPE_MAX_CNT,	//
};

enum MeshPassType	
{  
	MESH_DEPTH_PASS = 0,
	MESH_POINT_SHADOW_PASS,
    MESH_DIRECTIONAL_SHADOW_PASS,
	MESH_G_BUFFER_PASS,
	MESH_FORWARD_PASS,
    MESH_TRANSPARENT_PASS,

	MESH_PASS_TYPE_MAX_CNT,	//
};

class RenderPass
{
public:
	RenderPass() = default;
	~RenderPass() {};

	virtual void Init() {};

	virtual void Build(RDGBuilder& builder) {};

	virtual std::string GetName() { return "Unknown"; }

	virtual PassType GetType() = 0;

	bool IsEnabled()			{ return enable; }
	void SetEnable(bool enable) { this->enable = enable; }

private:
	bool enable = true;
};

#define EnablePassEditourUI() \
friend class PassWidget;