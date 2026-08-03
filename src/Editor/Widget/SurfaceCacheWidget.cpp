
#include "SurfaceCacheWidget.h"

#include "Core/SurfaceCache/SurfaceCache.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/Sampler.h"
#include "Platform/Input/InputSystem.h"
#include <cstdint>
#include <memory>


#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_vulkan.h"


void OnDrawCallback(const ImDrawList* ParentList, const ImDrawCmd* CMD)
{

}

void SurfaceCacheWidget::UI()
{
    static bool open = false;
    if(ImGui::Button("Show Surface Cache"))
    {
        open = true;
    }

    if(open) UIInternel(&open);
}

void SurfaceCacheWidget::UIInternel(bool* open)
{ 
    if(!ImGui::Begin("Surface Cache##1", open, ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar))
    {
        ImGui::End();
        return;
    }

    static SamplerRef sampler;
    static VkDescriptorSet handle[5];
    static ImVec2 imageSize = { SURFACE_CACHE_SIZE, SURFACE_CACHE_SIZE };
    static uint32_t index = 0;
    static float scaleFactor = 0.25f;

    // if(EngineContext::Input()->KeyIsPressed(KEY_TYPE_Z)) imageSize *= 0.95;
    // if(EngineContext::Input()->KeyIsPressed(KEY_TYPE_X)) imageSize /= 0.95;

    if(!sampler)
    {
        sampler = std::make_shared<Sampler>(
            ADDRESS_MODE_CLAMP_TO_EDGE,   
            FILTER_TYPE_LINEAR, 
            MIPMAP_MODE_LINEAR,
            0.0f);

        for(int i = 0; i < 5; i++)
        {
            handle[i] = ImGui_ImplVulkan_AddTexture(                   // TODO 太丑了，该写在RHI层
            (VkSampler)sampler->sampler->RawHandle(), 
            (VkImageView)EngineContext::RenderResource()->GetSurfaceCacheTexture(i)->textureView->RawHandle(), 
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }  
    }

    if(ImGui::Button("Switch")) index = (index + 1) % 5;
    ImGui::SameLine();
    ImGui::DragFloat("Scale", &scaleFactor, 0.005f, 0.01f, 10.0f);
    //ImGui::GetWindowDrawList()->AddCallback(OnDrawCallback, nullptr); // 添加回调来设置管线状态以处理通道问题？？
    ImGui::Image((ImTextureID)handle[index], imageSize * scaleFactor);
    //ImGui::GetWindowDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    
    ImGui::End();
}