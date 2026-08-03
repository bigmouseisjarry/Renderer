#pragma once

#include "Function/Render/RenderPass/BloomPass.h"
#include "Function/Render/RenderPass/ClipmapVisualizePass.h"
#include "Function/Render/RenderPass/NRDPass.h"
#include "Function/Render/RenderPass/PathTracingPass.h"
#include "Function/Render/RenderPass/PostProcessingPass.h"
#include "Function/Render/RenderPass/DeferredLightingPass.h"
#include "Function/Render/RenderPass/ReSTIRDIPass.h"
#include "Function/Render/RenderPass/ReSTIRGIPass.h"
#include "Function/Render/RenderPass/FXAAPass.h"
#include "Function/Render/RenderPass/SSSRPass.h"
#include "Function/Render/RenderPass/SVGFPass.h"
#include "Function/Render/RenderPass/TAAPass.h"
#include "Function/Render/RenderPass/GBufferPass.h"
#include "Function/Render/RenderPass/ClipmapPass.h"
#include "Function/Render/RenderPass/GPUCullingPass.h"
#include "Function/Render/RenderPass/ExposurePass.h"
#include "Function/Render/RenderPass/RayTracingBasePass.h"
#include "Function/Render/RenderPass/RenderPass.h"
#include "Function/Render/RenderPass/VolumetricFogPass.h"

#include <memory>

class PassWidget
{
public:
    static void UI(std::shared_ptr<RenderPass> pass);

private:
    static void GPUCullingPassUI(std::shared_ptr<GPUCullingPass> pass);
    static void GBufferPassUI(std::shared_ptr<GBufferPass> pass);
    static void ClipmapPassUI(std::shared_ptr<ClipmapPass> pass);  
    static void DeferredLightingPassUI(std::shared_ptr<DeferredLightingPass> pass);
    static void SSSRPassUI(std::shared_ptr<SSSRPass> pass);  
    static void VolumetricFogPassUI(std::shared_ptr<VolumetricFogPass> pass);  
    static void ReSTIRDIPassUI(std::shared_ptr<ReSTIRDIPass> pass);  
    static void ReSTIRGIPassUI(std::shared_ptr<ReSTIRGIPass> pass);  
    static void SVGFPassUI(std::shared_ptr<SVGFPass> pass); 
    static void NRDPassUI(std::shared_ptr<NRDPass> pass); 
    static void ClipmapVisualizePassUI(std::shared_ptr<ClipmapVisualizePass> pass);  
    static void PathTracingPassUI(std::shared_ptr<PathTracingPass> pass);
    static void BloomPassUI(std::shared_ptr<BloomPass> pass);
    static void FXAAPassUI(std::shared_ptr<FXAAPass> pass);
    static void TAAPassUI(std::shared_ptr<TAAPass> pass);  
    static void ExposurePassUI(std::shared_ptr<ExposurePass> pass);
    static void PostProcessingPassUI(std::shared_ptr<PostProcessingPass> pass);  
    static void RayTracingBasePassUI(std::shared_ptr<RayTracingBasePass> pass);  
};