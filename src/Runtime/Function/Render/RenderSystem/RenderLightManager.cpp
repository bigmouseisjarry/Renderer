#include "RenderLightManager.h"
#include "Core/Math/BoundingBox.h"
#include "Function/Global/Definations.h"
#include "Function/Global/EngineContext.h"
#include <array>
#include <cstdint>

void RenderLightManager::Init()
{

}

void RenderLightManager::Tick()
{
    PrepareLights();
}

std::shared_ptr<DirectionalLightComponent> RenderLightManager::GetDirectionalLight()                 
{ 
    return perframeLights[EngineContext::ThreadPool()->ThreadFrameIndex()].directionalLight; 
}

const std::vector<std::shared_ptr<VolumeLightComponent>>& RenderLightManager::GetVolumeLights()     
{ 
    return perframeLights[EngineContext::ThreadPool()->ThreadFrameIndex()].volumeLights; 
}

void RenderLightManager::PrepareLights()
{
    ENGINE_TIME_SCOPE(RenderLightManager::PrepareLights);

    LightSetting setting = {};
    setting.directionalLightCnt = 0;
    setting.pointLightCnt = 0;

    auto& lights = perframeLights[EngineContext::ThreadPool()->ThreadFrameIndex()];

    lights.directionalLight = nullptr;
    lights.volumeLights.clear();

    // 收集光源信息,更新参数
    // TODO 场景的CPU端剔除

    lights.directionalLight = EngineContext::World()->GetActiveScene()->GetDirectionalLight();
    if(lights.directionalLight && lights.directionalLight->Enable())
    {
        lights.directionalLight->UpdateLightInfo();
        setting.directionalLightCnt = 1;
    }

    setting.areaLightCnt = 0;                                                    // 面光块预留恒0
    setting.sunSlot = setting.directionalLightCnt > 0 ? 0u : 0xFFFFFFFFu;        // 太阳=方向灯块首槽

    auto pointLightComponents = EngineContext::World()->GetActiveScene()->GetPointLights();
    uint32_t denseIndex = setting.directionalLightCnt;   // 局部灯块起点=D
    uint32_t lightListVersion = 2166136261u;   // FNV-1a：活跃持久ID序列哈希，成员或顺序一变哈希即变
    lightListVersion = (lightListVersion ^ setting.directionalLightCnt) * 16777619u;
    lightListVersion = (lightListVersion ^ setting.areaLightCnt) * 16777619u;
    for(auto& pointLight : pointLightComponents)
    {
        if(pointLight && pointLight->Enable())
        {
            if(denseIndex - setting.directionalLightCnt >= MAX_ZBIN_LOCAL_LIGHTS || denseIndex >= 256u)
            {
                if(!zbinLightCapWarned)
                {
                    ENGINE_LOG_WARN("ZBin: active point lights exceed {} (entry index field), surplus dropped this frame", MAX_ZBIN_LOCAL_LIGHTS);
                    zbinLightCapWarned = true;
                }
                break;
            }

            pointLight->UpdateLightInfo(denseIndex++);
            setting.pointLightCnt++;

            lightListVersion = (lightListVersion ^ pointLight->pointLightID) * 16777619u;
        }
    }
    setting.lightListVersion     = lightListVersion;
    setting.prevLightListVersion = lastLightListVersion;    // ReSTIR时域守卫：上一渲染帧的灯集语义
    lastLightListVersion         = lightListVersion;

    uint32_t volumeLightIDs[MAX_VOLUME_LIGHT_COUNT] = { 0 };        
    uint32_t volumeLightCnt = 0;
    auto volumeLightComponents = EngineContext::World()->GetActiveScene()->GetVolumeLights();
    for(auto& volumeLight : volumeLightComponents)
    {
        if(volumeLight && volumeLight->Enable())
        {
            volumeLightIDs[volumeLightCnt] = volumeLight->volumeLightID;
            volumeLightCnt++;
            lights.volumeLights.push_back(volumeLight);

            volumeLight->UpdateLightInfo();
        }
    }

    // 提交整体处理的信息
    EngineContext::RenderResource()->SetLightSetting(setting);
    EngineContext::RenderResource()->SetVolumeLightSetting(volumeLightCnt, volumeLightIDs);
}