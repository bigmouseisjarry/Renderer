#include "PassWidget.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RenderPass/BloomPass.h"
#include "Function/Render/RenderPass/PostProcessingPass.h"
#include "Function/Render/RenderPass/FXAAPass.h"
#include "Function/Render/RenderPass/RayTracingBasePass.h"
#include "Function/Render/RenderPass/RenderPass.h"
#include "Function/Render/RenderPass/SVGFPass.h"
#include "NRDSettings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <implot.h>
#include <imgui.h>

#include <cstdint>
#include <string>

void PassWidget::UI(std::shared_ptr<RenderPass> pass)
{
    ImGui::PushID(pass.get());
    if (ImGui::CollapsingHeader(pass->GetName().c_str(), ImGuiTreeNodeFlags_None))
    {
        bool enabled = pass->IsEnabled();
        ImGui::Checkbox("Enable ", &enabled);
        pass->SetEnable(enabled);

        switch (pass->GetType()) {                                                                                                                                         
        case DEPTH_PASS:                                                                                                                break;
        case GPU_CULLING_PASS:          GPUCullingPassUI(std::static_pointer_cast<GPUCullingPass>(pass));                   break;
        case POINT_SHADOW_PASS:                                                                                                         break;
        case DIRECTIONAL_SHADOW_PASS:                                                                                                   break;
        case CLIPMAP_PASS:              ClipmapPassUI(std::static_pointer_cast<ClipmapPass>(pass));                         break;
        case G_BUFFER_PASS:             GBufferPassUI(std::static_pointer_cast<GBufferPass>(pass));                         break;
        case DEFERRED_LIGHTING_PASS:    DeferredLightingPassUI(std::static_pointer_cast<DeferredLightingPass>(pass));       break;
        case SSSR_PASS:                 SSSRPassUI(std::static_pointer_cast<SSSRPass>(pass));                               break;
        case VOLUMETIRC_FOG_PASS:       VolumetricFogPassUI(std::static_pointer_cast<VolumetricFogPass>(pass));             break;
        case RESTIR_DI_PASS:            ReSTIRDIPassUI(std::static_pointer_cast<ReSTIRDIPass>(pass));                       break;
        case RESTIR_GI_PASS:            ReSTIRGIPassUI(std::static_pointer_cast<ReSTIRGIPass>(pass));                       break;
        case SVGF_PASS:                 SVGFPassUI(std::static_pointer_cast<SVGFPass>(pass));                               break;
        case NRD_PASS:                  NRDPassUI(std::static_pointer_cast<NRDPass>(pass));                                 break;
        case FORWARD_PASS:                                                                                                              break;
        case TRANSPARENT_PASS:                                                                                                          break;
        case CLIPMAP_VISUALIZE_PASS:    ClipmapVisualizePassUI(std::static_pointer_cast<ClipmapVisualizePass>(pass));       break;
        case PATH_TRACING_PASS:         PathTracingPassUI(std::static_pointer_cast<PathTracingPass>(pass));                 break;
        case BLOOM_PASS:                BloomPassUI(std::static_pointer_cast<BloomPass>(pass));                             break;
        case FXAA_PASS:                 FXAAPassUI(std::static_pointer_cast<FXAAPass>(pass));                               break;
        case TAA_PASS:                  TAAPassUI(std::static_pointer_cast<TAAPass>(pass));                                 break;
        case EXPOSURE_PASS:             ExposurePassUI(std::static_pointer_cast<ExposurePass>(pass));                       break;
        case POST_PROCESSING_PASS:      PostProcessingPassUI(std::static_pointer_cast<PostProcessingPass>(pass));           break;
        case RAY_TRACING_BASE_PASS:     RayTracingBasePassUI(std::static_pointer_cast<RayTracingBasePass>(pass));           break;
        case EDITOR_UI_PASS:                                                                                                            break;
        case PRESENT_PASS:                                                                                                              break;
        default:                                                                                                                        break;
        }
    }
    ImGui::PopID();
}

std::string MeshPassName(MeshPassType type)
{
    switch (type) {
    case MESH_DEPTH_PASS:               return "Depth";
    case MESH_POINT_SHADOW_PASS:        return "Point Shadow";
    case MESH_DIRECTIONAL_SHADOW_PASS:  return "Dir Shadow";
    case MESH_G_BUFFER_PASS:            return "G-Buffer";
    case MESH_FORWARD_PASS:             return "Forward";
    case MESH_TRANSPARENT_PASS:         return "Transparent";
    default:                            return "";
    }
}

void PassWidget::GPUCullingPassUI(std::shared_ptr<GPUCullingPass> pass)
{
    ImGui::DragFloat("Lod error rate", &pass->lodSetting.lodErrorRate, 0.002f);
    ImGui::DragFloat("Lod error offset", &pass->lodSetting.lodErrorOffset, 0.002f);

    bool disableVirtualMeshCulling = pass->lodSetting.disableVirtualMeshCulling > 0 ? true : false;
    bool disableFrustrumCulling = pass->lodSetting.disableFrustrumCulling > 0 ? true : false;
    bool disableOcclusionCulling = pass->lodSetting.disableOcclusionCulling > 0 ? true : false;
    bool showBoundingBox = pass->lodSetting.showBoundingBox > 0 ? true : false;

    ImGui::Checkbox("Disable virtual mesh culling", &disableVirtualMeshCulling);
    ImGui::Checkbox("Disable frustrum culling", &disableFrustrumCulling);
    ImGui::Checkbox("Disable occlusion culling", &disableOcclusionCulling);
    ImGui::Checkbox("Show bounding box", &showBoundingBox);


    pass->lodSetting.disableVirtualMeshCulling = disableVirtualMeshCulling ? 1 : 0;
    pass->lodSetting.disableFrustrumCulling = disableFrustrumCulling ? 1 : 0;
    pass->lodSetting.disableOcclusionCulling = disableOcclusionCulling ? 1 : 0;
    pass->lodSetting.showBoundingBox = showBoundingBox ? 1 : 0;
    

    ImGui::Checkbox("Statistics", &pass->enableStatistics);
	if (pass->enableStatistics)
	{
		static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;

        ImGui::SeparatorText("Meshes: ");
        if (ImGui::BeginTable("table1", 5, flags))
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Mesh pass");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("Processed");
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("Frustum");
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("Occlu");
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("Rendered");

            for(uint32_t i = 0; i < pass->readBackDatas.size(); i++)
            {
                std::string name = MeshPassName((MeshPassType)i);
                for(auto& readBackData : pass->readBackDatas[i])
                {        
                    auto& setting = readBackData.meshSetting;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", setting.processSize);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", setting.frustumCull);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d", setting.occlusionCull);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%d", setting.drawSize);
                }
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Clusters: ");
        if (ImGui::BeginTable("table2", 5, flags))
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Mesh pass");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("Processed");
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("Frustum");
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("Occlu");
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("Rendered");

            for(uint32_t i = 0; i < pass->readBackDatas.size(); i++)
            {
                std::string name = MeshPassName((MeshPassType)i);
                for(auto& readBackData : pass->readBackDatas[i])
                {        
                    auto& setting = readBackData.clusterSetting;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", setting.processSize);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", setting.frustumCull);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d", setting.occlusionCull);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%d", setting.drawSize);
                }
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Cluster groups: ");
        if (ImGui::BeginTable("table3", 5, flags))
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Mesh pass");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("Processed");
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("Frustum");
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("Occlu");
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("Rendered");

            for(uint32_t i = 0; i < pass->readBackDatas.size(); i++)
            {
                std::string name = MeshPassName((MeshPassType)i);
                for(auto& readBackData : pass->readBackDatas[i])
                {        
                    auto& setting = readBackData.clusterGroupSetting;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", setting.processSize);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", setting.frustumCull);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d", setting.occlusionCull);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%d", setting.drawSize);
                }
            }
            ImGui::EndTable();
        }
	}
}

void PassWidget::GBufferPassUI(std::shared_ptr<GBufferPass> pass)
{

}

void PassWidget::ClipmapPassUI(std::shared_ptr<ClipmapPass> pass)
{
    ImGui::Checkbox("Attach to camera", &pass->attachToCamera);
    if(ImGui::Button("Full update")) pass->fullUpdate = true;
}

void PassWidget::DeferredLightingPassUI(std::shared_ptr<DeferredLightingPass> pass)
{
    bool directOnly = pass->setting.directOnly > 0 ? true : false;
    ImGui::Checkbox("Direct lighting only", &directOnly);
    pass->setting.directOnly = directOnly ? 1 : 0;

	if (ImGui::RadioButton("Color", pass->setting.mode == 0))
		pass->setting.mode = 0;
	ImGui::SameLine();
	if (ImGui::RadioButton("Normal", pass->setting.mode == 1))
		pass->setting.mode = 1;
	ImGui::SameLine();
	if (ImGui::RadioButton("Roughness/Metallic", pass->setting.mode == 2))
		pass->setting.mode = 2;

    //////////////////////////////////
	if (ImGui::RadioButton("World pos", pass->setting.mode == 3))
		pass->setting.mode = 3;
	ImGui::SameLine();
	if (ImGui::RadioButton("Shadow", pass->setting.mode == 4))
		pass->setting.mode = 4;
	ImGui::SameLine();
	if (ImGui::RadioButton("Linear depth", pass->setting.mode == 5))
		pass->setting.mode = 5;
	ImGui::SameLine();
	if (ImGui::RadioButton("Shadow cascade", pass->setting.mode == 6))
		pass->setting.mode = 6;

    //////////////////////////////////
	if (ImGui::RadioButton("Cluster light count", pass->setting.mode == 7))
		pass->setting.mode = 7;
    ImGui::SameLine();
	if (ImGui::RadioButton("Cluster XY", pass->setting.mode == 8))
		pass->setting.mode = 8;
	ImGui::SameLine();
	if (ImGui::RadioButton("Cluster Z", pass->setting.mode == 9))
		pass->setting.mode = 9;

    //////////////////////////////////
	if (ImGui::RadioButton("Indirect light", pass->setting.mode == 10))
		pass->setting.mode = 10;
	ImGui::SameLine();
	if (ImGui::RadioButton("Indirect irradiance", pass->setting.mode == 11))
		pass->setting.mode = 11;
	ImGui::SameLine();
	if (ImGui::RadioButton("Direct light", pass->setting.mode == 12))
		pass->setting.mode = 12;
}

void PassWidget::SSSRPassUI(std::shared_ptr<SSSRPass> pass)
{
    bool enableSkybox = pass->setting.enableSkybox > 0 ? true : false;
    bool reflectionOnly = pass->setting.reflectionOnly > 0 ? true : false;
    bool visualizeHitUV = pass->setting.visualizeHitUV > 0 ? true : false;
    bool visualizeHitMask = pass->setting.visualizeHitMask > 0 ? true : false;

    if (ImGui::RadioButton("Trace HiZ", pass->setting.mode == 0))
		pass->setting.mode = 0; 
    ImGui::SameLine();
    if (ImGui::RadioButton("Ray trace surface cache", pass->setting.mode == 1))
		pass->setting.mode = 1; 
    ImGui::SameLine();
    if (ImGui::RadioButton("Hybrid trace", pass->setting.mode == 2))
		pass->setting.mode = 2; 

	ImGui::Checkbox("Enable skybox ibl", &enableSkybox);
    ImGui::Checkbox("Reflection only", &reflectionOnly);
    ImGui::Checkbox("Visualize hit UV", &visualizeHitUV);
    ImGui::Checkbox("Visualize hit mask", &visualizeHitMask);

	ImGui::DragInt("Max mip", &pass->setting.maxMip, 1.0f, 0, 10);
	ImGui::DragInt("Start mip", &pass->setting.startMip, 1.0f, 0, 10);
	ImGui::DragInt("End mip", &pass->setting.endMip, 1.0f, 0, 10);
    ImGui::DragInt("Max loop", &pass->setting.maxLoop, 1.0f, 0, 200);

	ImGui::DragFloat("Max Roughness", &pass->setting.maxRoughness, 0.005f, 0.0f, 1.0f);
    ImGui::DragFloat("Min hit distance", &pass->setting.minHitDistance, 0.001f, 0.0f, 10.0f);
	ImGui::DragFloat("Importance sample bias", &pass->setting.importanceSampleBias, 0.005f, 0.0f, 1.0f);
	ImGui::DragFloat("Thickness", &pass->setting.thickness, 0.01f, -1.0f, 1.0f);
    ImGui::DragFloat("Screen fade", &pass->setting.screenFade, 0.1f, 0.0f, 1.0f);
	ImGui::DragFloat("Filter blend", &pass->setting.filterBlend, 0.1f, 0.0f, 1.0f);
	
    pass->setting.enableSkybox = enableSkybox ? 1 : 0;
    pass->setting.reflectionOnly = reflectionOnly ? 1 : 0;
    pass->setting.visualizeHitUV = visualizeHitUV ? 1 : 0;
    pass->setting.visualizeHitMask = visualizeHitMask ? 1 : 0;
}

void PassWidget::VolumetricFogPassUI(std::shared_ptr<VolumetricFogPass> pass)
{
    bool isotropic = pass->setting.isotropic > 0 ? true : false;
    bool fogOnly = pass->setting.fogOnly > 0 ? true : false;

    ImGui::Checkbox("Fog only", &fogOnly);
    ImGui::Checkbox("Isotropic scattering", &isotropic);
    if (!isotropic)	ImGui::DragFloat("Henyey greenstein G", &pass->setting.henyeyGreensteinG, 0.1f, -1.0f, 1.0f);

    ImGui::DragFloat("Attenuation factor", &pass->setting.attenuationFactor, 0.001f, 0.0f, 1.0f);
    ImGui::DragFloat("Scattering intencity", &pass->setting.scatteringIntencity, 0.001f, 0.0f, 1.0f);
    ImGui::DragFloat("Blend factor", &pass->setting.blendFactor, 0.001f, 0.0f, 1.0f);

    pass->setting.isotropic = isotropic ? 1 : 0;
    pass->setting.fogOnly = fogOnly ? 1 : 0;
}

void PassWidget::ReSTIRDIPassUI(std::shared_ptr<ReSTIRDIPass> pass)
{
    bool spatialReuse = pass->setting.spatialReuse > 0 ? true : false;
    bool temporalReuse = pass->setting.temporalReuse > 0 ? true : false;
    bool visibilityReuse = pass->setting.visibilityReuse > 0 ? true : false;
    bool unbias = pass->setting.unbias > 0 ? true : false;
    bool bruteLighting = pass->setting.bruteLighting > 0 ? true : false;
    bool showWeight = pass->setting.showWeight > 0 ? true : false;
    bool showLightID = pass->setting.showLightID > 0 ? true : false;

    ImGui::Checkbox("Spatial reuse", &spatialReuse);
    ImGui::Checkbox("Temporal reuse", &temporalReuse);
    ImGui::Checkbox("Visibility reuse", &visibilityReuse);
    ImGui::Checkbox("Unbias", &unbias);
    ImGui::Checkbox("Brute lighting", &bruteLighting);
    ImGui::Checkbox("Show weight", &showWeight);
    ImGui::Checkbox("Show light ID", &showLightID);

    ImGui::DragInt("Initial light sample count", &pass->setting.initialSampleCount, 0.1f, 0);

    ImGui::DragInt("Temporal sample count multiplier", &pass->setting.temporalSampleCountMultiplier, 0.1f, 0);
    ImGui::DragFloat("Temporal position threshold", &pass->setting.temporalPosThreshold, 0.1f, 0);

    ImGui::Text("Spatial neighbor count: 4 (fixed)");
    ImGui::DragFloat("Spatial position threshold", &pass->setting.spatialPosThreshold, 0.1f, 0);
    ImGui::DragFloat("Spatial normal threshold", &pass->setting.spatialNormalThreshold, 0.1f, 0);
    ImGui::DragFloat("Spatial radius", &pass->setting.spatialRadius, 0.1f, 0);

    pass->setting.spatialReuse = spatialReuse ? 1 : 0;
    pass->setting.temporalReuse = temporalReuse ? 1 : 0;
    pass->setting.visibilityReuse = visibilityReuse ? 1 : 0;
    pass->setting.unbias = unbias ? 1 : 0;
    pass->setting.bruteLighting = bruteLighting ? 1 : 0;
    pass->setting.showWeight = showWeight ? 1 : 0;
    pass->setting.showLightID = showLightID ? 1 : 0;
}

void PassWidget::ReSTIRGIPassUI(std::shared_ptr<ReSTIRGIPass> pass)
{
    bool spatialReuse = pass->setting.spatialReuse > 0 ? true : false;
    bool temporalReuse = pass->setting.temporalReuse > 0 ? true : false;
    bool visibilityReuse = pass->setting.visibilityReuse > 0 ? true : false;
    bool unbias = pass->setting.unbias > 0 ? true : false;
    bool enableSkybox = pass->setting.enableSkybox > 0 ? true : false;
    bool showRadiance = pass->setting.showRadiance > 0 ? true : false;

    ImGui::Checkbox("Spatial reuse", &spatialReuse);
    ImGui::Checkbox("Temporal reuse", &temporalReuse);
    ImGui::Checkbox("Visibility reuse", &visibilityReuse);
    ImGui::Checkbox("Unbias", &unbias);
    ImGui::Checkbox("Enable skybox", &enableSkybox);
    ImGui::Checkbox("Show Radiance", &showRadiance);

    ImGui::DragInt("Initial light sample count", &pass->setting.initialSampleCount, 0.1f, 0);

    ImGui::DragInt("Temporal sample count multiplier", &pass->setting.temporalSampleCountMultiplier, 0.1f, 0);
    ImGui::DragFloat("Temporal position threshold", &pass->setting.temporalPosThreshold, 0.1f, 0);

    ImGui::Text("Spatial neighbor count: 4 (fixed)");
    ImGui::DragFloat("Spatial position threshold", &pass->setting.spatialPosThreshold, 0.1f, 0);
    ImGui::DragFloat("Spatial normal threshold", &pass->setting.spatialNormalThreshold, 0.1f, 0);
    ImGui::DragFloat("Spatial radius", &pass->setting.spatialRadius, 0.1f, 0);

    ImGui::DragFloat("Surface Cache Factor", &pass->setting.surfaceCacheFactor, 0.01f, 0);

    pass->setting.spatialReuse = spatialReuse ? 1 : 0;
    pass->setting.temporalReuse = temporalReuse ? 1 : 0;
    pass->setting.visibilityReuse = visibilityReuse ? 1 : 0;
    pass->setting.unbias = unbias ? 1 : 0;
    pass->setting.enableSkybox = enableSkybox ? 1 : 0;
    pass->setting.showRadiance = showRadiance ? 1 : 0;
}

void PassWidget::SVGFPassUI(std::shared_ptr<SVGFPass> pass)
{
    ImGui::DragInt("ATrous filter round", &pass->setting.maxRound, 0.1f, 0);

    ImGui::DragFloat("ATrous sigma L", &pass->setting.sigmaL, 0.1f, 0);
    ImGui::DragFloat("ATrous sigma N", &pass->setting.sigmaN, 0.1f, 0);
    ImGui::DragFloat("ATrous sigma P", &pass->setting.sigmaP, 0.1f, 0);

    ImGui::DragFloat("Blend alpha", &pass->setting.alpha, 0.1f, 0);

    ImGui::DragInt("Output mode", &pass->setting.mode, 0.1f, 0);

    bool disocclusionFix = pass->setting.disocclusionFix > 0 ? true : false;
    bool antiFirefly = pass->setting.antiFirefly > 0 ? true : false;
    bool historyClamp = pass->setting.historyClamp > 0 ? true : false;

    ImGui::Checkbox("Denoised only", &pass->denoisedOnly);
    ImGui::Checkbox("Disocclusion fix", &disocclusionFix);
    ImGui::Checkbox("Anti-Firefly", &antiFirefly);
    ImGui::Checkbox("History clamp (TAA-like)", &historyClamp);  

    pass->setting.disocclusionFix = disocclusionFix ? 1 : 0;
    pass->setting.antiFirefly = antiFirefly ? 1 : 0;
    pass->setting.historyClamp = historyClamp ? 1 : 0;
}

void DragUint(const char* label, uint32_t* v, float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0)
{
    int data = (int)*v;
    ImGui::DragInt(label, &data, v_speed, v_min, v_max, format, flags);
    *v = (uint32_t)data;
}

void PassWidget::NRDPassUI(std::shared_ptr<NRDPass> pass)
{
    if (ImGui::CollapsingHeader("NRD Relax", ImGuiTreeNodeFlags_None))
    {
        if(ImGui::Button("Reset"))
            pass->relaxSettings = {};

        ImGui::DragFloat("accelerationAmount", &pass->relaxSettings.antilagSettings.accelerationAmount, 0.01f, 0, 1);
        ImGui::DragFloat("spatialSigmaScale", &pass->relaxSettings.antilagSettings.spatialSigmaScale, 0.05f, 0);
        ImGui::DragFloat("temporalSigmaScale", &pass->relaxSettings.antilagSettings.temporalSigmaScale, 0.05f, 0);
        ImGui::DragFloat("resetAmount", &pass->relaxSettings.antilagSettings.resetAmount, 0.01f, 0, 1);

        DragUint("diffuseMaxAccumulatedFrameNum", &pass->relaxSettings.diffuseMaxAccumulatedFrameNum, 1.0f, 0, nrd::RELAX_MAX_HISTORY_FRAME_NUM);
        DragUint("specularMaxAccumulatedFrameNum", &pass->relaxSettings.specularMaxAccumulatedFrameNum, 1.0f, 0, nrd::RELAX_MAX_HISTORY_FRAME_NUM);

        DragUint("diffuseMaxFastAccumulatedFrameNum", &pass->relaxSettings.diffuseMaxFastAccumulatedFrameNum, 1.0f, 0, pass->relaxSettings.diffuseMaxAccumulatedFrameNum);
        DragUint("specularMaxFastAccumulatedFrameNum", &pass->relaxSettings.specularMaxFastAccumulatedFrameNum, 1.0f, 0, pass->relaxSettings.specularMaxAccumulatedFrameNum);
        DragUint("historyFixFrameNum", &pass->relaxSettings.historyFixFrameNum, 1.0f, 0, 3);
        DragUint("historyFixBasePixelStride", &pass->relaxSettings.historyFixBasePixelStride, 1.0f, 1);

        DragUint("historyFixAlternatePixelStride", &pass->relaxSettings.historyFixAlternatePixelStride, 1.0f, 1);
        ImGui::DragFloat("historyFixEdgeStoppingNormalPower", &pass->relaxSettings.historyFixEdgeStoppingNormalPower, 0.1f, 1);
        ImGui::DragFloat("fastHistoryClampingSigmaScale", &pass->relaxSettings.fastHistoryClampingSigmaScale, 0.1f, 1, 3);
        ImGui::DragFloat("diffusePrepassBlurRadius", &pass->relaxSettings.diffusePrepassBlurRadius, 1.0f, 0);
        ImGui::DragFloat("specularPrepassBlurRadius", &pass->relaxSettings.specularPrepassBlurRadius, 1.0f, 0);
        ImGui::DragFloat("minHitDistanceWeight", &pass->relaxSettings.minHitDistanceWeight, 0.005f, 0.0001, 0.2);

        DragUint("spatialVarianceEstimationHistoryThreshold", &pass->relaxSettings.spatialVarianceEstimationHistoryThreshold, 1.0f, 0);

        ImGui::DragFloat("diffusePhiLuminance", &pass->relaxSettings.diffusePhiLuminance, 1.0f, 0);
        ImGui::DragFloat("specularPhiLuminance", &pass->relaxSettings.specularPhiLuminance, 1.0f, 0);
        ImGui::DragFloat("lobeAngleFraction", &pass->relaxSettings.lobeAngleFraction, 0.001f, 0, 1);
        ImGui::DragFloat("roughnessFraction", &pass->relaxSettings.roughnessFraction, 0.001f, 0, 1);
        ImGui::DragFloat("specularVarianceBoost", &pass->relaxSettings.specularVarianceBoost, 0.01f, 0);
        ImGui::DragFloat("specularLobeAngleSlack", &pass->relaxSettings.specularLobeAngleSlack, 0.01f);

        DragUint("atrousIterationNum", &pass->relaxSettings.atrousIterationNum, 1.0f, 2, 8);

        ImGui::DragFloat("diffuseMinLuminanceWeight", &pass->relaxSettings.diffuseMinLuminanceWeight, 0.01f, 0, 1);
        ImGui::DragFloat("specularMinLuminanceWeight", &pass->relaxSettings.specularMinLuminanceWeight, 0.01f, 0, 1);
        ImGui::DragFloat("depthThreshold", &pass->relaxSettings.depthThreshold, 0.001f, 0, 1);
        ImGui::DragFloat("confidenceDrivenRelaxationMultiplier", &pass->relaxSettings.confidenceDrivenRelaxationMultiplier, 0.01f);
        ImGui::DragFloat("confidenceDrivenLuminanceEdgeStoppingRelaxation", &pass->relaxSettings.confidenceDrivenLuminanceEdgeStoppingRelaxation, 0.01f);
        ImGui::DragFloat("confidenceDrivenNormalEdgeStoppingRelaxation", &pass->relaxSettings.confidenceDrivenNormalEdgeStoppingRelaxation, 0.01f);
        ImGui::DragFloat("luminanceEdgeStoppingRelaxation", &pass->relaxSettings.luminanceEdgeStoppingRelaxation, 0.01f);
        ImGui::DragFloat("normalEdgeStoppingRelaxation", &pass->relaxSettings.normalEdgeStoppingRelaxation, 0.01f);
        ImGui::DragFloat("roughnessEdgeStoppingRelaxation", &pass->relaxSettings.roughnessEdgeStoppingRelaxation, 0.01f);
    }

    if (ImGui::CollapsingHeader("NRD Reblur", ImGuiTreeNodeFlags_None))
    {
        if(ImGui::Button("Reset"))
            pass->reblurSettings = {};

        DragUint("maxAccumulatedFrameNum", &pass->reblurSettings.maxAccumulatedFrameNum, 1.0f, 0); 
        DragUint("maxFastAccumulatedFrameNum", &pass->reblurSettings.maxFastAccumulatedFrameNum, 1.0f, 0, pass->reblurSettings.maxFastAccumulatedFrameNum); 
        DragUint("maxStabilizedFrameNum", &pass->reblurSettings.maxStabilizedFrameNum, 1.0f, 0, pass->reblurSettings.maxFastAccumulatedFrameNum); 
        DragUint("historyFixFrameNum", &pass->reblurSettings.historyFixFrameNum, 1.0f, 0, 3); 
        DragUint("historyFixBasePixelStride", &pass->reblurSettings.historyFixBasePixelStride, 1.0f, 1); 
        DragUint("historyFixAlternatePixelStride", &pass->reblurSettings.historyFixAlternatePixelStride, 1.0f, 1); 
        
        ImGui::DragFloat("fastHistoryClampingSigmaScale", &pass->reblurSettings.fastHistoryClampingSigmaScale, 0.01f, 1, 3);  
        ImGui::DragFloat("diffusePrepassBlurRadius", &pass->reblurSettings.diffusePrepassBlurRadius, 0.01f, 0);  
        ImGui::DragFloat("specularPrepassBlurRadius", &pass->reblurSettings.specularPrepassBlurRadius, 0.01f, 0);  
        ImGui::DragFloat("minHitDistanceWeight", &pass->reblurSettings.minHitDistanceWeight, 0.01f, 0, 0.2);  
        ImGui::DragFloat("minBlurRadius", &pass->reblurSettings.minBlurRadius, 0.01f, 0);  
        ImGui::DragFloat("maxBlurRadius", &pass->reblurSettings.maxBlurRadius, 0.01f, 0);  
        ImGui::DragFloat("lobeAngleFraction", &pass->reblurSettings.lobeAngleFraction, 0.01f, 0, 1);  
        ImGui::DragFloat("roughnessFraction", &pass->reblurSettings.roughnessFraction, 0.01f, 0, 1);  
        ImGui::DragFloat("planeDistanceSensitivity", &pass->reblurSettings.planeDistanceSensitivity, 0.01f, 0, 1);  
        ImGui::DragFloat("fireflySuppressorMinRelativeScale", &pass->reblurSettings.fireflySuppressorMinRelativeScale, 0.01f, 1, 3);  
    }

    ImGui::DragFloat("Split screen", &pass->setting.splitScreen, 0.01f, 0, 1);
    ImGui::DragFloat("Motion vector scale X", &pass->setting.motionVectorScaleX, 0.01f, 0, 5);
    ImGui::DragFloat("Motion vector scale Y", &pass->setting.motionVectorScaleY, 0.01f, 0, 5);
    ImGui::DragFloat("Motion vector scale Z", &pass->setting.motionVectorScaleZ, 0.01f, 0, 5);

    bool enableAntiFirefly = pass->setting.enableAntiFirefly > 0 ? true : false;
    bool isBaseColorMetalnessAvailable = pass->setting.isBaseColorMetalnessAvailable > 0 ? true : false;
    bool demodulate = pass->setting.demodulate > 0 ? true : false;
    
    ImGui::Checkbox("Anti-Firefly", &enableAntiFirefly);
    ImGui::Checkbox("Enable Color/Metallic", &isBaseColorMetalnessAvailable); 
    ImGui::Checkbox("Demodulate", &demodulate); 
    ImGui::Checkbox("Denoised only", &pass->denoisedOnly);  
    ImGui::Checkbox("Debug", &pass->debug);  

    pass->setting.enableAntiFirefly = enableAntiFirefly ? 1 : 0;
    pass->setting.isBaseColorMetalnessAvailable = isBaseColorMetalnessAvailable ? 1 : 0;
    pass->setting.demodulate = demodulate ? 1 : 0;
}

void PassWidget::ClipmapVisualizePassUI(std::shared_ptr<ClipmapVisualizePass> pass)
{
    if (ImGui::RadioButton("Rasterize Voxel", pass->mode == 0))
		pass->mode = 0;
	ImGui::SameLine();
    if (ImGui::RadioButton("Ray tracing", pass->mode == 1))
		pass->mode = 1;

    ImGui::DragInt("Min visualize mipLevel", &pass->setting.minMip, 0.1f, 0, 10);
    ImGui::DragInt("Max visualize mipLevel", &pass->setting.maxMip, 0.1f, 0, 10);
}

void PassWidget::PathTracingPassUI(std::shared_ptr<PathTracingPass> pass)
{
    bool sampleSkyBox = pass->setting.sampleSkyBox > 0 ? true : false;
    bool indirectOnly = pass->setting.indirectOnly > 0 ? true : false;
    bool diffuseOnly = pass->setting.diffuseOnly > 0 ? true : false;

    if(ImGui::Button("Refresh")) pass->setting.totalNumSamples = 0;
    ImGui::SameLine();
    ImGui::Text("Total num samples: %d", pass->setting.totalNumSamples);
    ImGui::DragInt("Num samples", &pass->setting.numSamples, 0.01f);
    ImGui::DragInt("Num bounce", &pass->setting.numBounce, 0.01f);

    ImGui::Checkbox("Sample skybox", &sampleSkyBox);
    ImGui::Checkbox("Indirect only", &indirectOnly);
    ImGui::Checkbox("Diffuse only", &diffuseOnly);
    ImGui::DragInt("Mode", &pass->setting.mode, 0.01f);

    pass->setting.sampleSkyBox = sampleSkyBox ? 1 : 0;
    pass->setting.indirectOnly = indirectOnly ? 1 : 0;
    pass->setting.diffuseOnly = diffuseOnly ? 1 : 0;
}

void PassWidget::BloomPassUI(std::shared_ptr<BloomPass> pass)
{
    bool bloomOnly = pass->setting.bloomOnly > 0 ? true : false;

    ImGui::Checkbox("Bloom only", &bloomOnly);
	ImGui::DragFloat("Blur stride", &pass->setting.stride, 0.1f, 0.0f, 5.0f);
	ImGui::DragFloat("Mip bias", &pass->setting.bias, 0.1f, 0.0f, 10.0f);
	ImGui::DragFloat("Accumulate intencity", &pass->setting.accumulateIntencity, 0.1f, 0.0f, 5.0f);
	ImGui::DragFloat("Combine intencity", &pass->setting.combineIntencity, 0.1f, 0.0f, 1.0f);
    ImGui::DragFloat("Threshold min", &pass->setting.thresholdMin, 0.1f, 0.0f, 5.0f);
    ImGui::DragFloat("Threshold max", &pass->setting.thresholdMax, 0.1f, 1.0f, 100.0f);

    pass->setting.bloomOnly = bloomOnly ? 1 : 0;
}

void PassWidget::FXAAPassUI(std::shared_ptr<FXAAPass> pass)
{

}

void PassWidget::TAAPassUI(std::shared_ptr<TAAPass> pass)
{
    bool sharpen = pass->setting.sharpen > 0 ? true : false;
    bool reprojectionOnly = pass->setting.reprojectionOnly > 0 ? true : false;
    bool showVelocity = pass->setting.showVelocity > 0 ? true : false;
    bool showReprojectionVaild = pass->setting.showReprojectionVaild > 0 ? true : false;

	ImGui::Checkbox("Sharpen", &sharpen);
	ImGui::SameLine();
	ImGui::Checkbox("Reprojection only", &reprojectionOnly);
	ImGui::SameLine();
    ImGui::Checkbox("Reprojection valid", &showReprojectionVaild);

	ImGui::Checkbox("Show velocity", &showVelocity);
	ImGui::DragFloat("Velocity factor", &pass->setting.velocityFactor, 1.0f);
	ImGui::DragFloat("Blend factor", &pass->setting.blendFactor, 0.01f);

    pass->setting.sharpen = sharpen ? 1 : 0;
    pass->setting.reprojectionOnly = reprojectionOnly ? 1 : 0;
    pass->setting.showReprojectionVaild = showReprojectionVaild ? 1 : 0;
    pass->setting.showVelocity = showVelocity ? 1 : 0;
}

void PassWidget::ExposurePassUI(std::shared_ptr<ExposurePass> pass)
{
    ImGui::DragFloat("Min luminance", &pass->minLuminance, 0.01f);
	ImGui::DragFloat("Max luminance", &pass->maxLuminance, 0.1f);
	ImGui::DragFloat("Adjust speed", &pass->adjustSpeed, 0.1f);
    ImGui::Checkbox("Statistics", &pass->enableStatistics);
	if (pass->enableStatistics)
    {
        ImGui::Text("Avarage luminance: %f", pass->readBackData.luminance);   
        ImGui::Text("Adapted avarage luminance: %f", pass->readBackData.adaptedLuminance);   
        
        float luminanceMax = 0.0f;
        float luminance = log2(pass->readBackData.luminance);
        float adaptedLuminance = log2(pass->readBackData.adaptedLuminance);

        std::array<float, 256> xs;
        std::array<float, 256> counts;
        auto extent = EngineContext::Render()->GetWindowsExtent();
        uint32_t pixelCount = extent.width * extent.height;
        for(uint32_t i = 0; i < 256; i++)
        {
            xs[i] = pass->readBackData.setting.minLog2Luminance + (pass->readBackData.setting.luminanceRange / 256) * i;
            counts[i] = (float)pass->readBackData.readBackHistogramBuffer[i] / pixelCount * 100; 

            luminanceMax = std::max(luminanceMax, counts[i]);
        }
        if (ImPlot::BeginPlot("")) 
        {
            ImPlot::SetupAxes("log2 luminance","count(%)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotBars("##0", xs.data(), counts.data(), 256, (pass->readBackData.setting.luminanceRange / 256));
            ImPlot::SetNextFillStyle(ImVec4(1,1,0,1));
            ImPlot::PlotBars("##1", &adaptedLuminance, &luminanceMax, 1, (pass->readBackData.setting.luminanceRange / 512));
            ImPlot::SetNextFillStyle(ImVec4(1,1,1,1));
            ImPlot::PlotBars("##2", &luminance, &luminanceMax, 1, (pass->readBackData.setting.luminanceRange / 512));  
            ImPlot::EndPlot();
        } 
    }
}

void PassWidget::PostProcessingPassUI(std::shared_ptr<PostProcessingPass> pass)
{
    bool fixLuminance = pass->setting.fixLuminance > 0 ? true : false;

    if (ImGui::RadioButton("CryEngine", pass->setting.mode == 0))
		pass->setting.mode = 0;
	ImGui::SameLine();
	if (ImGui::RadioButton("Uncharted", pass->setting.mode == 1))
		pass->setting.mode = 1;
	ImGui::SameLine();
	if (ImGui::RadioButton("ACES", pass->setting.mode == 2))
		pass->setting.mode = 2;
	ImGui::SameLine();
	if (ImGui::RadioButton("None", pass->setting.mode == 3))
		pass->setting.mode = 3;

    ImGui::Checkbox("Fix luminance", &fixLuminance);

    ImGui::DragFloat("Exposure", &pass->setting.exposure, 0.01f);
    ImGui::DragFloat("Luminance", &pass->setting.luminance, 0.01f);
    ImGui::DragFloat("Saturation", &pass->setting.saturation, 0.1f);
	ImGui::DragFloat("Contrast", &pass->setting.contrast, 0.1f);

    pass->setting.fixLuminance = fixLuminance ? 1 : 0;
}

void PassWidget::RayTracingBasePassUI(std::shared_ptr<RayTracingBasePass> pass)
{
	if (ImGui::RadioButton("Diffuse", pass->setting.mode == 0))
		pass->setting.mode = 0;
	ImGui::SameLine();
	if (ImGui::RadioButton("Normal", pass->setting.mode == 1))
		pass->setting.mode = 1;
	ImGui::SameLine();
	if (ImGui::RadioButton("Roughness/Metallic", pass->setting.mode == 2))
		pass->setting.mode = 2;

	if (ImGui::RadioButton("World pos", pass->setting.mode == 3))
		pass->setting.mode = 3;
	ImGui::SameLine();
	if (ImGui::RadioButton("World dist", pass->setting.mode == 4))
		pass->setting.mode = 4;

    ImGui::SeparatorText("Surface cache");
    if (ImGui::RadioButton("Diffuse##1", pass->setting.mode == 5))
		pass->setting.mode = 5; 
    ImGui::SameLine();   
    if (ImGui::RadioButton("Normal##1", pass->setting.mode == 6))
		pass->setting.mode = 6; 
    ImGui::SameLine();  
    if (ImGui::RadioButton("Roughness/Metallic##1", pass->setting.mode == 7))
		pass->setting.mode = 7; 
    
    if (ImGui::RadioButton("Emission##1", pass->setting.mode == 8))
		pass->setting.mode = 8;    
    ImGui::SameLine();  
    if (ImGui::RadioButton("Lighting##1", pass->setting.mode == 9))
		pass->setting.mode = 9;   
}