#include "MeshRendererComponent.h"
#include "Core/Event/Event.h"
#include "Core/Math/BoundingBox.h"
#include "Core/Math/Math.h"
#include "Function/Framework/Component/TransformComponent.h"
#include "Function/Framework/Component/TryGetComponent.h"
#include "Function/Global/Definations.h"
#include "Function/Global/EngineContext.h"
#include "Resource/Asset/Asset.h"
#include <cstdint>
#include <memory>

CEREAL_REGISTER_TYPE(MeshRendererComponent)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, MeshRendererComponent)

MeshRendererComponent::~MeshRendererComponent()
{
    if(!EngineContext::Destroyed()) 
    {
        for(auto& objectID : objectIDs) EngineContext::RenderResource()->ReleaseObjectID(objectID); 
        objectIDs.clear();

        for(auto& meshCardID : meshCardIDs) 
        {
            EngineContext::Render()->GetSurfaceCacheManager()->ReleaseCache(meshCardID);
            EngineContext::RenderResource()->ReleaseMeshCardID(meshCardID); 
        }
        meshCardIDs.clear();
    }
}

void MeshRendererComponent::OnLoad()
{
    BeginLoadAssetBind()
    ResizeAssetArray(materials)
    LoadAssetBind(Model, model)
    LoadAssetArrayBind(Material, materials)
    EndLoadAssetBind
}

void MeshRendererComponent::OnSave()
{
    BeginSaveAssetBind()
    SaveAssetBind(model)
    SaveAssetArrayBind(materials)
    EndSaveAssetBind
}

void MeshRendererComponent::InitResource()
{
    updateTicks = -1;

    for(auto& objectID : objectIDs) EngineContext::RenderResource()->ReleaseObjectID(objectID); 
    objectIDs.clear();
    objectInfos.clear();
    currentModels.clear();
    prevModels.clear();
    if(model)
    {   
        uint32_t submeshCount = model->GetSubmeshCount();
        materials.resize(submeshCount);
        currentModels.resize(submeshCount);
        prevModels.resize(submeshCount);
        objectInfos.resize(submeshCount);  
        while(objectIDs.size() < submeshCount)
        {
            objectIDs.push_back(EngineContext::RenderResource()->AllocateObjectID());   // TODO 需要保证连续
            meshCardIDs.push_back(EngineContext::RenderResource()->AllocateMeshCardID());
        }

        for(uint32_t i = 0; i < submeshCount; i++)  // 逐子物体更新物体信息
        {
            Vec4 localScale = Vec4::Ones();
            localScale.x() = model->Submesh(i).scale.x();
            localScale.y() = model->Submesh(i).scale.y();
            localScale.z() = model->Submesh(i).scale.z();

            objectInfos[i] = {
                .animationID = 0,
                .materialID = materials[i] ? materials[i]->GetMaterialID() : 0,
                .vertexID = model->Submesh(i).vertexBuffer->vertexID,
                .indexID = model->Submesh(i).indexBuffer->indexID,
                .meshCardID = meshCardIDs[i],
                .sphere = model->Submesh(i).mesh->sphere,
                .box = model->Submesh(i).mesh->box,
                .localScale = localScale,
                .debugData = Vec4::Zero()
            };
        }
    }
}

void MeshRendererComponent::OnInit()
{
    Component::OnInit();
    InitResource();
}

void MeshRendererComponent::OnUpdate(float deltaTime)
{
    InitComponentIfNeed();
}

void MeshRendererComponent::SetModel(ModelRef model) 					
{ 
    this->model = model; 
    InitResource();
}

void MeshRendererComponent::SetMaterial(MaterialRef material, uint32_t index)
{
    if(materials.size() <= index) 
    {
        this->materials.resize(index + 1);
    }
    materials[index] = material;
}

void MeshRendererComponent::SetMaterials(std::vector<MaterialRef> materials, uint32_t firstIndex)
{
    if(this->materials.size() <= firstIndex + materials.size()) 
    {
        this->materials.resize(firstIndex + materials.size() + 1);
    }
    for(uint32_t i = 0; i < materials.size(); i++)
    {
        uint32_t index = i + firstIndex;
        this->materials[index] = materials[i];
    }
}

MaterialRef MeshRendererComponent::GetMaterial(uint32_t index)
{
    if(materials.size() > index) return materials[index];
    return nullptr;
}

void MeshRendererComponent::CollectDrawBatch(std::vector<DrawBatch>& batches)
{
    for(uint32_t i = 0; i < model->GetSubmeshCount(); i++)
    {   
        auto& submesh = model->Submesh(i);

        if(materials[i] != nullptr)
        {
            batches.emplace_back(
                objectIDs[i],
                submesh.vertexBuffer,
                submesh.indexBuffer,
                submesh.meshClusterID,
                submesh.meshClusterGroupID,
                materials[i]);
        }
    }

    // 此处涉及提交物体数据，需要等待帧栅栏////////////////////////////////////////////
    // TODO 换个位置写
    std::shared_ptr<TransformComponent> transformComponent = TryGetComponent<TransformComponent>();
    if(!transformComponent) return;

    Mat4 transform = transformComponent->GetModelMat();
    Vec3 scale = transformComponent->GetScale();
    bool update = (transform != prevModel) || (scale != prevScale) || (updateTicks == -1);

    Vec4 modelScale = Vec4::Ones();
    modelScale.x() = scale.x();
    modelScale.y() = scale.y();
    modelScale.z() = scale.z();

    if(update)
        updateTicks = 0;
    if(updateTicks <= FRAMES_IN_FLIGHT)
    {
        for(uint32_t i = 0; i < model->GetSubmeshCount(); i++)  // 逐子物体更新物体信息
        {
            currentModels[i] = transform * model->Submesh(i).transform;  // 很慢
            objectInfos[i].model = currentModels[i];  // 矩阵乘法慢？
            objectInfos[i].invModel = currentModels[i].inverse();
            //objectInfos[i].invModel = transformComponent->GetModelMatInv();
            objectInfos[i].prevModel = prevModels[i];
            objectInfos[i].modelScale = modelScale;

            prevModels[i] = currentModels[i];  
        }

        EngineContext::RenderResource()->SetObjectInfos(objectInfos, objectIDs[0]);
        updateTicks++; 
    }

    prevModel = transform;
    prevScale = scale;
}

void MeshRendererComponent::CollectAccelerationStructureInstance(std::vector<RHIAccelerationStructureInstanceInfo>& instances)
{
    std::shared_ptr<TransformComponent> transformComponent = TryGetComponent<TransformComponent>();
    if(!transformComponent) return;

    for(uint32_t i = 0; i < model->GetSubmeshCount(); i++)
    {   
        auto& submesh = model->Submesh(i);

        if(materials[i] != nullptr)
        {
            RHIAccelerationStructureInstanceInfo info = {};
            info.instanceIndex = objectIDs[i];
            info.mask = 0xFF;
            info.shaderBindingTableOffset = 0;
            info.blas = submesh.blas;
            Math::Mat3x4(currentModels[i], &info.transform[0][0]);   

            instances.push_back(info);
        }
    }
}

void MeshRendererComponent::CollectSurfaceCacheTask(std::vector<SurfaceCacheTask>& tasks)
{
    std::shared_ptr<TransformComponent> transformComponent = TryGetComponent<TransformComponent>();
    if(!transformComponent) return;

    for(uint32_t i = 0; i < model->GetSubmeshCount(); i++)
    {   
        auto& submesh = model->Submesh(i);

        if(materials[i] != nullptr)
        {
            Mat4 model = objectInfos[i].model;
            Vec4 pos = model * Vec4(submesh.mesh->sphere.center.x(), 
                                    submesh.mesh->sphere.center.y(), 
                                    submesh.mesh->sphere.center.z(), 1.0f);

            tasks.emplace_back(
                meshCardIDs[i],
                objectIDs[i], 
                materials[i]->GetMaterialID(),
                transformComponent->GetScale().array() * submesh.scale.array(),
                Vec3(pos.x(), pos.y(), pos.z()),
                submesh.mesh->box,
                submesh.mesh->sphere,
                submesh.vertexBuffer,
                submesh.indexBuffer,
                false);
        }
    }
}
