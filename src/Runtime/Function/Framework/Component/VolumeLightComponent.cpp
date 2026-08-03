
#include "VolumeLightComponent.h"
#include "Core/Math/Math.h"
#include "Function/Framework/Component/TransformComponent.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "TryGetComponent.h"

CEREAL_REGISTER_TYPE(VolumeLightComponent)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, VolumeLightComponent)

VolumeLightComponent::~VolumeLightComponent()
{
    if(!EngineContext::Destroyed() && volumeLightID != 0) EngineContext::RenderResource()->ReleaseVolumeLightID(volumeLightID);
}

void VolumeLightComponent::OnInit()
{
    Component::OnInit();

    volumeLightID = EngineContext::RenderResource()->AllocateVolumeLightID();
}

void VolumeLightComponent::OnUpdate(float deltaTime)
{
    InitComponentIfNeed();

	//UpdateLightInfo();

    for (int i = 0; i < 2; i++)	//检测更新频率
	{
		if (updateFrequences[i] != 0) 
		{
			updateCnts[i]++;
			if (updateCnts[i] >= updateFrequences[i])
			{
				updateCnts[i] = 0; 
                shouldUpdate[i] = true;

			} else {
                shouldUpdate[i] = false;
            }
		} else {
            shouldUpdate[i] = false;
        }    
	}
}

void VolumeLightComponent::UpdateLightInfo()
{
    std::shared_ptr<TransformComponent> transformComponent = TryGetComponent<TransformComponent>();
    if(!transformComponent) return;

	//更新包围信息
	{
        Vec3 extent = { (probeCounts.x() - 1) * gridStep.x(),
                        (probeCounts.y() - 1) * gridStep.y(),
                        (probeCounts.z() - 1) * gridStep.z()};
		box = BoundingBox(transformComponent->GetPosition() - extent / 2.0f, transformComponent->GetPosition() + extent / 2.0f);
	}

	//更新DDGI信息
	{
        info.setting.gridStartPosition = box.minBound;	//起始点为最小坐标
        info.setting.gridStep = gridStep;
        info.setting.probeCounts = probeCounts;

        info.setting.depthSharpness = depthSharpness;
        info.setting.blendWeight = blendWeight;
        info.setting.normalBias = normalBias;
        info.setting.energyPreservation = energyPreservation;
        
		info.setting.maxProbeDistance = std::max(std::max(gridStep.x(), gridStep.y()), gridStep.z()) * 1.5;		//探针的最大有效范围，用于限制深度
        info.setting.raysPerProbe = raysPerProbe;

        info.setting.enable = enable;
        info.setting.visibilityTest = visibilityTest;
        info.setting.infiniteBounce = infiniteBounce;
        info.setting.randomOrientation = randomOrientation;

		info.setting.boundingBox = box;
	}

    EngineContext::RenderResource()->SetVolumeLightInfo(info, volumeLightID);
}

void VolumeLightComponent::UpdateTexture()
{
    InitComponentIfNeed();

    info.setting.probeCounts = probeCounts;
    info.setting.raysPerProbe = raysPerProbe;

	//每个probe左上有1像素的padding，整张纹理左上有一像素padding
	info.setting.irradianceTextureWidth = (DDGI_IRRADIANCE_PROBE_SIZE + 2) * info.setting.probeCounts.x() * info.setting.probeCounts.y() + 2;	//水平放x，y两维
	info.setting.irradianceTextureHeight = (DDGI_IRRADIANCE_PROBE_SIZE + 2) * info.setting.probeCounts.z() + 2;								    //垂直放z一维

	info.setting.depthTextureWidth = (DDGI_DEPTH_PROBE_SIZE + 2) * info.setting.probeCounts.x() * info.setting.probeCounts.y() + 2;
	info.setting.depthTextureHeight = (DDGI_DEPTH_PROBE_SIZE + 2) * info.setting.probeCounts.z() + 2;

    {
        Extent3D extent = {	(uint32_t)(info.setting.raysPerProbe),
                            (uint32_t)(info.setting.probeCounts.x() * info.setting.probeCounts.y() * info.setting.probeCounts.z()),
                            1};

        textures.diffuseTex = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            FORMAT_R8G8B8A8_UNORM,
            extent,
            1, 1);

        textures.normalTex = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            FORMAT_R8G8B8A8_SNORM,
            extent,
            1, 1);

        textures.emissionTex = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            FORMAT_R16G16B16A16_SFLOAT,
            extent,
            1, 1);

        textures.positionTex = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            FORMAT_R16G16B16A16_SFLOAT,
            extent,
            1, 1);

        textures.radianceTex = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            FORMAT_R16G16B16A16_SFLOAT,
            extent,
            1, 1);
    }

    {
        Extent3D extent = {	info.setting.irradianceTextureWidth,
                            info.setting.irradianceTextureHeight,
                            1};

        textures.irradianceTex = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            FORMAT_R16G16B16A16_SFLOAT,
            extent,
            1, 1);
    }

    {
        Extent3D extent = {	info.setting.depthTextureWidth,
                            info.setting.depthTextureHeight,
                            1};

        textures.depthTex = std::make_shared<Texture>( 
            TEXTURE_TYPE_2D, 
            FORMAT_R16G16_SFLOAT,
            extent,
            1, 1);
    }

    EngineContext::RenderResource()->SetVolumeLightTextures(textures, volumeLightID);
}

