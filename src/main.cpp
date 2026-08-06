
#include "Core/Math/Math.h"
#include "Function/Framework/Component/MeshRendererComponent.h"
#include "Function/Framework/Component/PointLightComponent.h"
#include "Function/Framework/Component/TransformComponent.h"
#include "Function/Framework/Component/DirectionalLightComponent.h"
#include "Function/Framework/Component/SkyboxComponent.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Platform/Thread/QueuedWork.h"
#include "Runtime/Function/Framework/Component/ScriptComponent.h"
#include "Runtime/Function/Framework/Component/VolumeLightComponent.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RenderResource/Texture.h"
#include "Runtime/Function/Framework/Entity/Entity.h"
#include "Runtime/Function/Render/RenderPass/RenderPass.h"
#include "Runtime/Function/Render/RenderResource/Material.h"
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

void LoadScene()
{
    std::shared_ptr<Scene> scene = EngineContext::World()->LoadScene("Asset/BuildIn/Scene/default.asset");
    EngineContext::World()->SetActiveScene(scene->GetName());
}

void InitCornellBoxScene()
{
    std::shared_ptr<Scene> scene = EngineContext::World()->CreateNewScene("CornellBoxScene");

    // Camera
    if(true)
    {
        std::shared_ptr<Entity> camera                                  = scene->CreateEntity("Camera");
        std::shared_ptr<TransformComponent> transformComponent          = camera->TryGetComponent<TransformComponent>();
        std::shared_ptr<CameraComponent> cameraComponent                = camera->AddComponent<CameraComponent>();

        transformComponent->SetPosition({-21.0f, 10.0f, 2.0f});
    }

    // Directional light
    if(true)
    {
        std::shared_ptr<Entity> directionalLight                                = scene->CreateEntity("Directional Light"); 
        std::shared_ptr<TransformComponent> transformComponent                  = directionalLight->TryGetComponent<TransformComponent>();
        std::shared_ptr<DirectionalLightComponent> directionalLightComponent    = directionalLight->AddComponent<DirectionalLightComponent>();
        std::shared_ptr<SkyboxComponent> skyboxComponent                        = directionalLight->AddComponent<SkyboxComponent>();

        transformComponent->SetPosition({-20.0f, 3.0f, 0.0f});
        transformComponent->SetRotation({20.0f, 50.0f, -70.0f});

        directionalLightComponent->SetIntencity(3.7f);

        std::vector<std::string> skyboxPaths;
        skyboxPaths.push_back("Asset/BuildIn/Texture/skybox/Factory_Sunset_Sky_Dome_LDR/px.png");
        skyboxPaths.push_back("Asset/BuildIn/Texture/skybox/Factory_Sunset_Sky_Dome_LDR/nx.png");
        skyboxPaths.push_back("Asset/BuildIn/Texture/skybox/Factory_Sunset_Sky_Dome_LDR/py.png");
        skyboxPaths.push_back("Asset/BuildIn/Texture/skybox/Factory_Sunset_Sky_Dome_LDR/ny.png");
        skyboxPaths.push_back("Asset/BuildIn/Texture/skybox/Factory_Sunset_Sky_Dome_LDR/pz.png");
        skyboxPaths.push_back("Asset/BuildIn/Texture/skybox/Factory_Sunset_Sky_Dome_LDR/nz.png");
        TextureRef skyboxTexture = std::make_shared<Texture>(skyboxPaths, TEXTURE_TYPE_CUBE);

        skyboxComponent->SetSkyboxTexture(skyboxTexture);
        skyboxComponent->SetIntencity(1.0f);
    }

    // Point Shadow light
    if(true)
    {
        std::shared_ptr<Entity> pointLight                          = scene->CreateEntity("Point Shadow Light");
        std::shared_ptr<PointLightComponent> pointLightComponent    = pointLight->AddComponent<PointLightComponent>();
        std::shared_ptr<TransformComponent> transformComponent      = pointLight->TryGetComponent<TransformComponent>();

        transformComponent->SetPosition({0.6f, 5.2f, -6.3f});
        pointLightComponent->SetScale(30.0f);
        pointLightComponent->SetColor(Vec3(0.0f, 0.285f, 1.0f));
        pointLightComponent->SetIntencity(100.0f);
        // pointLightComponent->SetFogScattering(0.5f);
    }

    // Volume light
    if(true)
    {
        std::shared_ptr<Entity> volumeLight                             = scene->CreateEntity("Volume light"); 
        std::shared_ptr<TransformComponent> transformComponent          = volumeLight->TryGetComponent<TransformComponent>();
        std::shared_ptr<VolumeLightComponent> volumeLightComponent      = volumeLight->AddComponent<VolumeLightComponent>();

        transformComponent->SetPosition({0.0f, 13.0f, 2.85f});

        volumeLightComponent->SetUpdateFrequence(1);
        volumeLightComponent->SetBlendWeight(0.5f);
        volumeLightComponent->SetVisulaize(false);
    }

    // Sphere
    if(true)
    {
        ModelProcessSetting processSetting =  {
            .smoothNormal = true,
            .flipUV = false,
            .loadMaterials = false,
            .tangentSpace = true,
            .generateBVH = false,
            .generateCluster = false,
            //.generateVirtualMesh = true,
            .cacheCluster = false
        };
        std::shared_ptr<Model> model = std::make_shared<Model>("Asset/BuildIn/Model/Basic/sphere_div.obj", processSetting);

        std::shared_ptr<Entity> entity                                  = scene->CreateEntity("Sphere");
        std::shared_ptr<TransformComponent> transformComponent          = entity->TryGetComponent<TransformComponent>();
        std::shared_ptr<MeshRendererComponent> meshRendererComponent    = entity->AddComponent<MeshRendererComponent>();

        transformComponent->SetPosition({6.0f, 14.5f, 6.0f});
        transformComponent->SetRotation({0.0f, 0.0f, 0.0f});
        transformComponent->SetScale({3.0f, 3.0f, 3.0f});

        MaterialRef material = std::make_shared<Material>();
        material->SetMetallic(0.8f);
        material->SetRoughness(0.1f);

        meshRendererComponent->SetModel(model);
        meshRendererComponent->SetMaterials({material});
    }

    // Bunny
    if(true)
    {
        std::shared_ptr<Entity> entity                                  = scene->CreateEntity("Bunny");
        std::shared_ptr<MeshRendererComponent> meshRendererComponent    = entity->AddComponent<MeshRendererComponent>();
        std::shared_ptr<TransformComponent> transformComponent          = entity->TryGetComponent<TransformComponent>();

        transformComponent->SetPosition({-4.5f, 0.5f, -5.5f});
        transformComponent->SetScale({3.5f, 3.5f, 3.5f});

        ModelProcessSetting processSetting =  {
            .smoothNormal = true,
            .flipUV = false,
            .loadMaterials = false,
            .tangentSpace = true,
            .generateBVH = false,
            .generateCluster = false,
            //.generateVirtualMesh = true,
            .cacheCluster = false
        };
        std::shared_ptr<Model> model = std::make_shared<Model>("Asset/BuildIn/Model/stanford/bunny.obj", processSetting);

        MaterialRef material = std::make_shared<Material>();
        material->SetRoughness(0.55f);
        material->SetMetallic(1.0f);
        material->SetDiffuse({0.3f, 0.3f, 0.3f, 1.0f});

        meshRendererComponent->SetModel(model);
        meshRendererComponent->SetMaterial(material); 
    }

    // Lion
    if(true)
    {
        std::shared_ptr<Entity> entity                                  = scene->CreateEntity("Lion");
        std::shared_ptr<MeshRendererComponent> meshRendererComponent    = entity->AddComponent<MeshRendererComponent>();
        std::shared_ptr<TransformComponent> transformComponent          = entity->TryGetComponent<TransformComponent>();

        transformComponent->SetPosition({-8.0f, 0.5f, 6.0f});
        transformComponent->SetRotation({0.0f, -90.0f, 0.0f});
        transformComponent->SetScale({0.15f, 0.15f, 0.15f});

        ModelProcessSetting processSetting =  {
            .smoothNormal = true,
            .flipUV = true,
            .loadMaterials = false,
            .tangentSpace = true,
            .generateBVH = false,
            .generateCluster = false,
            //.generateVirtualMesh = true,
            .cacheCluster = false
        };
        std::shared_ptr<Model> model = std::make_shared<Model>("Asset/BuildIn/Model/lion_head/lion_head.fbx", processSetting);

        MaterialRef material = std::make_shared<Material>();
        material->SetDiffuse(std::make_shared<Texture>("Asset/BuildIn/Model/lion_head/textures/lion_head_diff_2k.png"));
        material->SetNormal(std::make_shared<Texture>("Asset/BuildIn/Model/lion_head/textures/lion_head_nor_gl_2k.png"));
        material->SetARM(std::make_shared<Texture>("Asset/BuildIn/Model/lion_head/textures/lion_head_arm_2k.png"));

        meshRendererComponent->SetModel(model);
        meshRendererComponent->SetMaterial(material); 
    }

    // emmisive box
    if(true)
    {
        ModelProcessSetting processSetting =  {
            .smoothNormal = false,
            .flipUV = false,
            .loadMaterials = false,
            .tangentSpace = true,
            .generateBVH = false,
            .generateCluster = false,
            .generateVirtualMesh = false,
            .cacheCluster = false
        };
        std::shared_ptr<Model> model = std::make_shared<Model>("Asset/BuildIn/Model/Basic/cube.obj", processSetting);

        std::shared_ptr<Entity> entity                                  = scene->CreateEntity("emmisive box");
        std::shared_ptr<TransformComponent> transformComponent          = entity->TryGetComponent<TransformComponent>();
        std::shared_ptr<MeshRendererComponent> meshRendererComponent    = entity->AddComponent<MeshRendererComponent>();
        std::shared_ptr<ScriptComponent> scriptComponent                = entity->AddComponent<ScriptComponent>();

        transformComponent->SetPosition({0.8, 18.5, -4.9});
        transformComponent->SetScale({3.0f, 0.1f, 3.0f});
        transformComponent->SetRotation({0.0f, 0.0f, 0.0f});

        MaterialRef material = std::make_shared<Material>();
        material->SetMetallic(0.0f);
        material->SetRoughness(1.0f);
        material->SetDiffuse({0.0f, 0.0f, 0.0f, 1.0f});
        material->SetEmission({0.8f, 1.0f, 0.8f, 30.0f});

        meshRendererComponent->SetModel(model);
        meshRendererComponent->SetMaterials({material});

        // scriptComponent->ScriptOnUpdate([material](std::shared_ptr<Entity> entry, float deltaTime){

        //     static float totalTime = 0.0f;
        //     totalTime += deltaTime / 1000.0f;

        //     auto func = [](float x){
        //         if(x < 2) return std::min(x, 1.0f);
        //         if(x < 4) return std::min(4.0f - x, 1.0f);
        //         return 0.0f;
        //     };

        //     float red = func(Math::Mode(totalTime, 6.0f));
        //     float green = func(Math::Mode(totalTime + 2.0f, 6.0f));
        //     float blue = func(Math::Mode(totalTime + 4.0f, 6.0f));

        //     material->SetEmission({ red, green, blue, 30.0f });
        // });
    }

    // Cornell Box
    if(true)
    {
        ModelProcessSetting processSetting =  {
            .smoothNormal = false,
            .flipUV = false,
            .loadMaterials = false,
            .tangentSpace = true,
            .generateBVH = false,
            .generateCluster = false,
            //.generateVirtualMesh = true,
            .cacheCluster = false
        };
        std::shared_ptr<Model> model = std::make_shared<Model>("Asset/BuildIn/Model/Basic/cube.obj", processSetting);

        // 下 左 右 前 后 上
        Vec3 positions[6] = {   Vec3(0.0, 0.0, 0.0),
                                Vec3(0.0, 9.5, 9.5),
                                Vec3(0.0, 9.5, -9.5),
                                Vec3(-9.5, 9.5, 0.0),
                                Vec3(10.5, 9.5, 0.0),
                                Vec3(0.0, 19.0, 0.0)};

        Vec3 scales[6] = {  Vec3(10.0, 0.5, 10.0),
                            Vec3(10.0, 9.0, 0.5),
                            Vec3(10.0, 9.0, 0.5),
                            Vec3(0.5, 10.0, 10.0),
                            Vec3(0.5, 10.0, 10.0),
                            Vec3(10.0, 0.5, 10.0)};

        Vec4 colors[6] = {  Vec4(1.0, 1.0, 1.0, 1.0),
                            Vec4(0.65, 0.05, 0.05, 1.0), // red
                            Vec4(0.12, 0.45, 0.15, 1.0), // green
                            Vec4(1.0, 1.0, 1.0, 1.0),
                            Vec4(1.0, 1.0, 1.0, 1.0),
                            Vec4(1.0, 1.0, 1.0, 1.0)};                   

        for(int i = 0; i < 6; i++)
        {
            std::shared_ptr<Entity> entity                                  = scene->CreateEntity("box " + std::to_string(i));
            std::shared_ptr<TransformComponent> transformComponent          = entity->TryGetComponent<TransformComponent>();
            std::shared_ptr<MeshRendererComponent> meshRendererComponent    = entity->AddComponent<MeshRendererComponent>();

            transformComponent->SetPosition(positions[i]);
            transformComponent->SetRotation({0.0f, 0.0f, 0.0f});
            transformComponent->SetScale(scales[i]);

            MaterialRef material = std::make_shared<Material>();
            material->SetDiffuse(colors[i]);
            material->SetMetallic(0.0f);
            material->SetRoughness(1.0f);
            if(i == 3)
            {
                transformComponent->SetPosition({2.0f, 8.0f, 0.0f});
                transformComponent->SetRotation({10.0f, -132.0f, 40.0f});
                transformComponent->SetScale({1.0f, 8.0f, 8.0f});
                //material->SetMetallic(0.0f);
                //material->SetRoughness(0.2f);
            }

            meshRendererComponent->SetModel(model);
            meshRendererComponent->SetMaterials({material});
        }
    }

    EngineContext::World()->SetActiveScene("CornellBoxScene");
}

void InitScene()
{
    std::shared_ptr<Scene> scene = EngineContext::World()->CreateNewScene("defaultScene");

    // Camera
    if(true)
    {
        std::shared_ptr<Entity> camera                                  = scene->CreateEntity("Camera");
        std::shared_ptr<TransformComponent> transformComponent          = camera->TryGetComponent<TransformComponent>();
        std::shared_ptr<CameraComponent> cameraComponent                = camera->AddComponent<CameraComponent>();

        transformComponent->SetPosition({-35.0f, 5.5f, 9.5f});
        transformComponent->SetRotation({0.0f, 19.0f, -11.0f});
    }

    // Directional light
    if(true)
    {
        std::shared_ptr<Entity> directionalLight                                = scene->CreateEntity("Directional Light"); 
        std::shared_ptr<TransformComponent> transformComponent                  = directionalLight->TryGetComponent<TransformComponent>();
        std::shared_ptr<DirectionalLightComponent> directionalLightComponent    = directionalLight->AddComponent<DirectionalLightComponent>();
        std::shared_ptr<SkyboxComponent> skyboxComponent                        = directionalLight->AddComponent<SkyboxComponent>();

        transformComponent->SetPosition({-20.0f, 3.0f, 0.0f});
        transformComponent->SetRotation({0.0f, 30.0f, -60.0f});

        directionalLightComponent->SetIntencity(3.7f);
        // directionalLightComponent->SetFogScattering(0.0f);

        std::vector<std::string> skyboxPaths;
        skyboxPaths.push_back("Asset/BuildIn/Texture/skybox/Factory_Sunset_Sky_Dome_LDR/px.png");
        skyboxPaths.push_back("Asset/BuildIn/Texture/skybox/Factory_Sunset_Sky_Dome_LDR/nx.png");
        skyboxPaths.push_back("Asset/BuildIn/Texture/skybox/Factory_Sunset_Sky_Dome_LDR/py.png");
        skyboxPaths.push_back("Asset/BuildIn/Texture/skybox/Factory_Sunset_Sky_Dome_LDR/ny.png");
        skyboxPaths.push_back("Asset/BuildIn/Texture/skybox/Factory_Sunset_Sky_Dome_LDR/pz.png");
        skyboxPaths.push_back("Asset/BuildIn/Texture/skybox/Factory_Sunset_Sky_Dome_LDR/nz.png");
        TextureRef skyboxTexture = std::make_shared<Texture>(skyboxPaths, TEXTURE_TYPE_CUBE);
        //EngineContext::Asset()->SaveAsset(skyboxTexture, "Asset/BuildIn/Texture/skybox/Factory_Sunset_Sky_Dome_LDR.asset");
        // TextureRef skyboxTexture = EngineContext::Asset()->GetOrLoadAsset<Texture>("Asset/BuildIn/Texture/skybox/Factory_Sunset_Sky_Dome_LDR.asset");

        skyboxComponent->SetSkyboxTexture(skyboxTexture);
        skyboxComponent->SetIntencity(1.0f);
    }

    // Volume light
    if(true)
    {
        std::shared_ptr<Entity> volumeLight                             = scene->CreateEntity("Volume light"); 
        std::shared_ptr<TransformComponent> transformComponent          = volumeLight->TryGetComponent<TransformComponent>();
        std::shared_ptr<VolumeLightComponent> volumeLightComponent      = volumeLight->AddComponent<VolumeLightComponent>();

        //transformComponent->SetPosition({8.6f, -4.0f, -4.9f});
        transformComponent->SetPosition({8.6f, 11.0f, -5.0f});

        volumeLightComponent->SetUpdateFrequence(4);
        volumeLightComponent->SetBlendWeight(0.5f);
        //volumeLightComponent->SetProbeCounts({21, 10, 18});
        volumeLightComponent->SetProbeCounts({21, 5, 15});
        volumeLightComponent->SetGridStep({6.0f, 6.0f, 6.0f});
        volumeLightComponent->SetVisulaize(false);
    }

    // Point Shadow light
    if(true)
    {
        std::shared_ptr<Entity> pointLight                          = scene->CreateEntity("Point Shadow Light 1");
        std::shared_ptr<PointLightComponent> pointLightComponent    = pointLight->AddComponent<PointLightComponent>();
        std::shared_ptr<TransformComponent> transformComponent      = pointLight->TryGetComponent<TransformComponent>();

        transformComponent->SetPosition({-20.0f, 3.0f, 8.5f});
        pointLightComponent->SetScale(15.0f);
        pointLightComponent->SetColor(Vec3(0.0f, 0.285f, 1.0f));
        pointLightComponent->SetIntencity(100.0f);
        // pointLightComponent->SetFogScattering(0.5f);
    }
    if(true)
    {
        std::shared_ptr<Entity> pointLight                          = scene->CreateEntity("Point Shadow Light 2");
        std::shared_ptr<PointLightComponent> pointLightComponent    = pointLight->AddComponent<PointLightComponent>();
        std::shared_ptr<TransformComponent> transformComponent      = pointLight->TryGetComponent<TransformComponent>();
        std::shared_ptr<ScriptComponent> scriptComponent            = pointLight->AddComponent<ScriptComponent>();

        transformComponent->SetPosition({-3.0f, 4.5f, 10.0f});
        pointLightComponent->SetScale(40.0f);
        pointLightComponent->SetColor(Vec3(1.0f, 0.66f, 0.0f));
        pointLightComponent->SetIntencity(100.0f);
        pointLightComponent->SetFogScattering(0.2f);

        scriptComponent->ScriptOnUpdate([](std::shared_ptr<Entity> e, float deltaTime){

            static float totalOffset = 0.0f;
            static float totalTime = 0.0f;
            totalTime -= deltaTime / 1666.0f;

            auto func = [](float x){
                if(x < 2) return std::min(x, 1.0f);
                if(x < 4) return std::min(4.0f - x, 1.0f);
                return 0.0f;
            };

            float red = func(Math::Mode(totalTime + 1.0f, 6.0f));
            float green = func(Math::Mode(totalTime + 3.0f, 6.0f));
            float blue = func(Math::Mode(totalTime + 5.0f, 6.0f));

            auto pointLightComponent = e->TryGetComponent<PointLightComponent>();
            pointLightComponent->SetColor({red, green, blue});

            double radians = totalTime * 60 * PI / 180.0;
            float offset = std::cos(radians) * 8;

            auto transformComponent = e->TryGetComponent<TransformComponent>();
            transformComponent->SetPosition(transformComponent->GetPosition() + Vec3(0, 0, offset - totalOffset));
            totalOffset = offset;
        });
    }
    if(false)
    {
        std::shared_ptr<Entity> pointLight                          = scene->CreateEntity("Point Shadow Light 3");
        std::shared_ptr<PointLightComponent> pointLightComponent    = pointLight->AddComponent<PointLightComponent>();
        std::shared_ptr<TransformComponent> transformComponent      = pointLight->TryGetComponent<TransformComponent>();

        transformComponent->SetPosition({-20.0f, 5.5f, 10.0f});
        pointLightComponent->SetScale(40.0f);
        pointLightComponent->SetColor(Vec3(1.0f, 1.0f, 1.0f));
        pointLightComponent->SetIntencity(100.0f);
        pointLightComponent->SetFogScattering(0.2f);
    }

    // Point light
    // for(int i = 0; i < 1000; i++)
    // {
    //     Vec3 pos = Vec3((i / 100) % 10, (i / 10) % 10, i % 10) * 5.0f;
    //     pos += Vec3(-25.0f, 5.0f, -10.0f);
    //     Vec3 color = Vec3((i / 100) % 10, (i / 10) % 10, i % 10) / 10.0f;

    //     std::shared_ptr<Entity> pointLight                          = scene->CreateEntity("Point Light " + std::to_string(i + 1));
    //     std::shared_ptr<PointLightComponent> pointLightComponent    = pointLight->AddComponent<PointLightComponent>();
    //     std::shared_ptr<TransformComponent> transformComponent      = pointLight->TryGetComponent<TransformComponent>();

    //     transformComponent->SetPosition(pos);
    //     pointLightComponent->SetScale(25.0f);
    //     pointLightComponent->SetColor(color);
    //     pointLightComponent->SetIntencity(50.0f);
    //     pointLightComponent->SetCastShadow(false);
    // }

    
    // Sphere
    if(true)
    {
        ModelProcessSetting processSetting =  {
            .smoothNormal = true,
            .flipUV = false,
            .loadMaterials = false,
            .tangentSpace = true,
            .generateBVH = false,
            .generateCluster = false,
            .generateVirtualMesh = true,
            .cacheCluster = false
        };
        std::shared_ptr<Model> model = std::make_shared<Model>("Asset/BuildIn/Model/Basic/sphere_div.obj", processSetting);

        Vec3 offsetY = Vec3(0, 1, 0) * 1.5;
        Vec3 offsetZ = Vec3(0, 0, 1) * 1.5;
        for(int i = 0; i < 6; i++)
        {
            for(int j = 0; j < 6; j++)
            {
                std::shared_ptr<Entity> entity                                  = scene->CreateEntity("Sphere[" + std::to_string(i) + "][" + std::to_string(j) + "]");
                std::shared_ptr<TransformComponent> transformComponent          = entity->TryGetComponent<TransformComponent>();
                std::shared_ptr<MeshRendererComponent> meshRendererComponent    = entity->AddComponent<MeshRendererComponent>();

                transformComponent->SetPosition(Vec3(-22.0f, 3.0f, 3.0f) + offsetY * i + offsetZ * j);
                transformComponent->SetRotation({0.0f, 0.0f, 0.0f});
                transformComponent->SetScale({0.5f, 0.5f, 0.5f});

                MaterialRef material = std::make_shared<Material>();
                material->SetMetallic(i * 0.2f);
                material->SetRoughness(j * 0.2f);
                //material->SetDiffuse({0.8f, 0.8f, 0.8f, 1.0f});

                meshRendererComponent->SetModel(model);
                meshRendererComponent->SetMaterials({material});
            }
        }
    }

    // Plane 1
    if(true)
    {
        std::shared_ptr<Entity> entity                                  = scene->CreateEntity("Plane 1");
        std::shared_ptr<TransformComponent> transformComponent          = entity->TryGetComponent<TransformComponent>();
        std::shared_ptr<MeshRendererComponent> meshRendererComponent    = entity->AddComponent<MeshRendererComponent>();

        transformComponent->SetPosition({-2.0f, -0.24f, 1.9f});
        transformComponent->SetRotation({0.0f, 0.0f, 0.0f});
        transformComponent->SetScale({30.0f, 5.0f, 20.0f});

        ModelProcessSetting processSetting =  {
            .smoothNormal = false,
            .flipUV = false,
            .loadMaterials = false,
            .tangentSpace = true,
            .generateBVH = false,
            .generateCluster = true,
            .generateVirtualMesh = false,
            .cacheCluster = false
        };
        std::shared_ptr<Model> model = std::make_shared<Model>("Asset/BuildIn/Model/Basic/plane.obj", processSetting);

        MaterialRef material = std::make_shared<Material>();
        // material->SetMetallic(0.2f);
        // material->SetRoughness(0.05f);
        material->SetMetallic(0.7f);
        material->SetRoughness(0.04f);

        meshRendererComponent->SetModel(model);
        meshRendererComponent->SetMaterials({material});
    }

    // Plane 2
    if(true)
    {
        std::shared_ptr<Entity> entity                                  = scene->CreateEntity("Plane 2");
        std::shared_ptr<TransformComponent> transformComponent          = entity->TryGetComponent<TransformComponent>();
        std::shared_ptr<MeshRendererComponent> meshRendererComponent    = entity->AddComponent<MeshRendererComponent>();

        transformComponent->SetPosition({-27.0f, 3.0f, -6.0f});
        transformComponent->SetRotation({90.0f, 0.0f, 0.0f});
        transformComponent->SetScale({3.0f, 3.0f, 3.0f});

        ModelProcessSetting processSetting =  {
            .smoothNormal = false,
            .flipUV = true,
            .loadMaterials = false,
            .tangentSpace = false,
            .generateBVH = false,
            .generateCluster = false,
            .generateVirtualMesh = true,
            .cacheCluster = false
        };
        std::shared_ptr<Model> model = std::make_shared<Model>("Asset/BuildIn/Model/Basic/plane_div.obj", processSetting);

        TextureRef texture = std::make_shared<Texture>("Asset/BuildIn/Texture/basic/checker.png");

        MaterialRef material = std::make_shared<Material>();
        material->SetDiffuse(texture);
        material->SetMetallic(0.0f);
        material->SetRoughness(1.0f);

        meshRendererComponent->SetModel(model);
        meshRendererComponent->SetMaterials({material});
    }

    // emmisive box
    if(true)
    {
        ModelProcessSetting processSetting =  {
            .smoothNormal = false,
            .flipUV = false,
            .loadMaterials = false,
            .tangentSpace = true,
            .generateBVH = false,
            .generateCluster = false,
            .generateVirtualMesh = false,
            .cacheCluster = false
        };
        std::shared_ptr<Model> model = std::make_shared<Model>("Asset/BuildIn/Model/Basic/cube.obj", processSetting);

        std::shared_ptr<Entity> entity                                  = scene->CreateEntity("emmisive box");
        std::shared_ptr<TransformComponent> transformComponent          = entity->TryGetComponent<TransformComponent>();
        std::shared_ptr<MeshRendererComponent> meshRendererComponent    = entity->AddComponent<MeshRendererComponent>();
        std::shared_ptr<ScriptComponent> scriptComponent                = entity->AddComponent<ScriptComponent>();

        transformComponent->SetPosition({-3.0, 3.5, 13.0});
        transformComponent->SetRotation({0.0f, 0.0f, 0.0f});
        transformComponent->SetScale({0.5f, 0.5f, 0.5f});

        MaterialRef material = std::make_shared<Material>();
        material->SetMetallic(0.0f);
        material->SetRoughness(1.0f);
        material->SetDiffuse({0.0f, 0.0f, 0.0f, 1.0f});
        material->SetEmission({0.8f, 1.0f, 0.8f, 20.0f});

        meshRendererComponent->SetModel(model);
        meshRendererComponent->SetMaterials({material});

        scriptComponent->ScriptOnUpdate([material](std::shared_ptr<Entity> e, float deltaTime){

            static float totalOffset = 0.0f;
            static float totalTime = 0.0f;
            totalTime += deltaTime / 1000.0f;

            auto func = [](float x){
                if(x < 2) return std::min(x, 1.0f);
                if(x < 4) return std::min(4.0f - x, 1.0f);
                return 0.0f;
            };

            float red = func(Math::Mode(totalTime, 6.0f));
            float green = func(Math::Mode(totalTime + 2.0f, 6.0f));
            float blue = func(Math::Mode(totalTime + 4.0f, 6.0f));

            material->SetEmission({ red, green, blue, 30.0f });

            double radians = totalTime * 60 * PI / 180.0;
            float offset = std::cos(radians) * 5;

            auto transformComponent = e->TryGetComponent<TransformComponent>();
            transformComponent->SetPosition(transformComponent->GetPosition() + Vec3(0, 0, offset - totalOffset));
            totalOffset = offset;
        });
    }

    // Klee
    if(true)
    {
        std::shared_ptr<Entity> entity                                  = scene->CreateEntity("Klee");
        std::shared_ptr<TransformComponent> transformComponent          = entity->TryGetComponent<TransformComponent>();
        std::shared_ptr<MeshRendererComponent> meshRendererComponent   = entity->AddComponent<MeshRendererComponent>();

        transformComponent->SetPosition({-20.1f, -0.2f, 7.9f});
        transformComponent->SetRotation({0.0f, -90.0f, 0.0f});
        transformComponent->SetScale({2.0f, 2.0f, 2.0f});

        ModelProcessSetting processSetting =  {
            .smoothNormal = false,
            .flipUV = false,
            .loadMaterials = false,
            .tangentSpace = true,
            .generateBVH = false,
            .generateCluster = false,
            .generateVirtualMesh = false,
            .cacheCluster = false
        };
        std::shared_ptr<Model> model = std::make_shared<Model>("Asset/BuildIn/Model/Klee/klee.fbx", processSetting);

        std::vector<TextureRef> textures;
        textures.push_back(std::make_shared<Texture>("Asset/BuildIn/Model/Klee/Texture/face.jpg")); 
        textures.push_back(std::make_shared<Texture>("Asset/BuildIn/Model/Klee/Texture/hair.jpg")); 
        textures.push_back(std::make_shared<Texture>("Asset/BuildIn/Model/Klee/Texture/dressing.jpg"));  

        std::vector<MaterialRef> materials1;
        materials1.push_back(std::make_shared<Material>());
        materials1.push_back(std::make_shared<Material>());
        materials1.push_back(std::make_shared<Material>());
        materials1[0]->SetDiffuse(textures[0]);
        materials1[1]->SetDiffuse(textures[1]);
        materials1[2]->SetDiffuse(textures[2]);

        meshRendererComponent->SetModel(model);
        meshRendererComponent->SetMaterials(materials1);
    }

    // Bunny
    if(true)
    {
        std::shared_ptr<Entity> entity                                  = scene->CreateEntity("Bunny");
        std::shared_ptr<MeshRendererComponent> meshRendererComponent    = entity->AddComponent<MeshRendererComponent>();
        std::shared_ptr<TransformComponent> transformComponent          = entity->TryGetComponent<TransformComponent>();

        transformComponent->SetPosition({-20.2f, 0.0f, 9.6f});
        transformComponent->SetScale({1.0f, 1.0f, 1.0f});

        ModelProcessSetting processSetting =  {
            .smoothNormal = true,
            .flipUV = false,
            .loadMaterials = false,
            .tangentSpace = true,
            .generateBVH = false,
            .generateCluster = false,
            .generateVirtualMesh = true,
            .cacheCluster = false
        };
        std::shared_ptr<Model> model = std::make_shared<Model>("Asset/BuildIn/Model/stanford/bunny.obj", processSetting);

        MaterialRef material = std::make_shared<Material>();

        meshRendererComponent->SetModel(model);
        meshRendererComponent->SetMaterial(material); 
    }

    // Scene
    if(true)
    {
        std::shared_ptr<Entity> entity                                  = scene->CreateEntity("Scene");
        std::shared_ptr<MeshRendererComponent> meshRendererComponent   = entity->AddComponent<MeshRendererComponent>();
        std::shared_ptr<TransformComponent> transformComponent          = entity->TryGetComponent<TransformComponent>();

        transformComponent->SetPosition({0.0f, 0.0f, 0.0f});
        transformComponent->SetScale({0.01f, 0.01f, 0.01f});

        ModelProcessSetting processSetting =  {
            .smoothNormal = false,
            .flipUV = true,
            .loadMaterials = true,
            .tangentSpace = true,
            .generateBVH = false,
            .generateCluster = false,
            .generateVirtualMesh = true,
            .cacheCluster = false,
            //.forcePngTexture = true
        };

        std::shared_ptr<Model> model = std::make_shared<Model>("Asset/BuildIn/Model/Scene/scene_fix_no_tree2.fbx", processSetting);
        
        std::vector<MaterialRef> materials;
        for(uint32_t i = 0; i < model->GetSubmeshCount(); i++)
        {
            //materials.push_back(std::make_shared<Material>());
            materials.push_back(model->GetMaterial(i));
        }

        meshRendererComponent->SetModel(model);
        meshRendererComponent->SetMaterials(materials);
    }

    // Material 
    std::array<MaterialRef, 20> materials;
    {
        int materialIndex = 0;
        materials[materialIndex] = std::make_shared<Material>();
        materials[materialIndex]->SetDiffuse(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/blue_metal_plate/blue_metal_plate_diff_2k.png"));
        materials[materialIndex]->SetNormal(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/blue_metal_plate/blue_metal_plate_nor_gl_2k.png"));
        materials[materialIndex]->SetARM(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/blue_metal_plate/blue_metal_plate_arm_2k.png"));
        //materials[materialIndex]->SetTextureScale({1.5f, 1.5f});

        materialIndex++;
        materials[materialIndex] = std::make_shared<Material>();
        materials[materialIndex]->SetDiffuse(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/coast_sand_rocks/coast_sand_rocks_diff_2k.png"));
        materials[materialIndex]->SetNormal(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/coast_sand_rocks/coast_sand_rocks_nor_gl_2k.png"));
        materials[materialIndex]->SetARM(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/coast_sand_rocks/coast_sand_rocks_arm_2k.png"));
        //materials[materialIndex]->SetTextureScale({1.5f, 1.5f});

        materialIndex++;
        materials[materialIndex] = std::make_shared<Material>();
        materials[materialIndex]->SetDiffuse(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/brick_pavement/brick_pavement_diff_2k.png"));
        materials[materialIndex]->SetNormal(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/brick_pavement/brick_pavement_nor_gl_2k.png"));
        materials[materialIndex]->SetARM(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/brick_pavement/brick_pavement_arm_2k.png"));
        //materials[materialIndex]->SetTextureScale({1.5f, 1.5f});

        materialIndex++;
        materials[materialIndex] = std::make_shared<Material>();
        materials[materialIndex]->SetDiffuse(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/glossy_metal/glossy_metal_diff_2k.png"));
        materials[materialIndex]->SetNormal(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/glossy_metal/glossy_metal_nor_gl_2k.png"));
        materials[materialIndex]->SetARM(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/glossy_metal/glossy_metal_arm_2k.png"));
        //materials[materialIndex]->SetTextureScale({1.5f, 1.5f});

        materialIndex++;
        materials[materialIndex] = std::make_shared<Material>();
        materials[materialIndex]->SetDiffuse(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/dirt/dirt_diff_2k.png"));
        materials[materialIndex]->SetNormal(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/dirt/dirt_nor_gl_2k.png"));
        materials[materialIndex]->SetARM(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/dirt/dirt_arm_2k.png"));
        //materials[materialIndex]->SetTextureScale({1.5f, 1.5f});

        //////////////////////////////////////////////////////////////////////////////////
        materialIndex++;
        materials[materialIndex] = std::make_shared<Material>();
        materials[materialIndex]->SetDiffuse(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/cloth/velour_velvet/velour_velvet_diff_2k.png"));
        materials[materialIndex]->SetNormal(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/cloth/velour_velvet/velour_velvet_nor_gl_2k.png"));
        materials[materialIndex]->SetARM(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/cloth/velour_velvet/velour_velvet_arm_2k.png"));
        materials[materialIndex]->SetTextureScale({1.5f, 1.5f});

        materialIndex++;
        materials[materialIndex] = std::make_shared<Material>();
        materials[materialIndex]->SetDiffuse(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/cloth/crepe_satin/crepe_satin_diff_2k.png"));
        materials[materialIndex]->SetNormal(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/cloth/crepe_satin/crepe_satin_nor_gl_2k.png"));
        materials[materialIndex]->SetARM(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/cloth/crepe_satin/crepe_satin_arm_2k.png"));
        materials[materialIndex]->SetTextureScale({1.5f, 1.5f});

        materialIndex++;
        materials[materialIndex] = std::make_shared<Material>();
        materials[materialIndex]->SetDiffuse(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/cloth/curly_teddy_checkered/curly_teddy_checkered_diff_2k.png"));
        materials[materialIndex]->SetNormal(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/cloth/curly_teddy_checkered/curly_teddy_checkered_nor_gl_2k.png"));
        materials[materialIndex]->SetARM(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/cloth/curly_teddy_checkered/curly_teddy_checkered_arm_2k.png"));
        materials[materialIndex]->SetTextureScale({2.0f, 2.0f});

        materialIndex++;
        materials[materialIndex] = std::make_shared<Material>();
        materials[materialIndex]->SetDiffuse(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/cloth/knitted_fleece/knitted_fleece_diff_2k.png"));
        materials[materialIndex]->SetNormal(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/cloth/knitted_fleece/knitted_fleece_nor_gl_2k.png"));
        materials[materialIndex]->SetARM(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/cloth/knitted_fleece/knitted_fleece_arm_2k.png"));
        materials[materialIndex]->SetTextureScale({1.5f, 1.5f});

        materialIndex++;
        materials[materialIndex] = std::make_shared<Material>();
        materials[materialIndex]->SetDiffuse(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/cloth/denim_fabric/denim_fabric_diff_2k.png"));
        materials[materialIndex]->SetNormal(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/cloth/denim_fabric/denim_fabric_nor_gl_2k.png"));
        materials[materialIndex]->SetARM(std::make_shared<Texture>("Asset/BuildIn/Texture/PBR/cloth/denim_fabric/denim_fabric_arm_2k.png"));
        materials[materialIndex]->SetTextureScale({1.5f, 1.5f});
    }

    // cloth
    if(true)
    {
        ModelProcessSetting processSetting =  {
            .smoothNormal = true,
            .flipUV = true,
            .loadMaterials = false,
            .tangentSpace = true,
            .generateBVH = false,
            .generateCluster = false,
            .generateVirtualMesh = true,
            .cacheCluster = false
        };
        std::shared_ptr<Model> model = std::make_shared<Model>("Asset/BuildIn/Model/Cloth/Cloth.fbx", processSetting);


        Vec3 offsetZ = Vec3(0, 0, 1) * 3;
        for(int i = 0; i < 5; i++)
        {
            std::shared_ptr<Entity> entity                                  = scene->CreateEntity("Cloth[" + std::to_string(i) + "]");
            std::shared_ptr<MeshRendererComponent> meshRendererComponent    = entity->AddComponent<MeshRendererComponent>();
            std::shared_ptr<TransformComponent> transformComponent          = entity->TryGetComponent<TransformComponent>();

            transformComponent->SetPosition(Vec3(-25.0f, 3.0f, 2.0f) + i * offsetZ);
            transformComponent->SetRotation({0.0f, -90.0f, 0.0f});
            transformComponent->SetScale({0.01f, 0.01f, 0.01f});

            MaterialRef material = materials[5 + i];

            meshRendererComponent->SetModel(model);
            meshRendererComponent->SetMaterial(material); 
        }
    }

    // material ball
    if(true)
    {
        ModelProcessSetting processSetting =  {
            .smoothNormal = true,
            .flipUV = false,
            .loadMaterials = false,
            .tangentSpace = true,
            .generateBVH = false,
            .generateCluster = false,
            .generateVirtualMesh = true,
            .cacheCluster = false
        };
        std::shared_ptr<Model> model = std::make_shared<Model>("Asset/BuildIn/Model/Basic/sphere_div.obj", processSetting);
        // std::shared_ptr<Model> model = std::make_shared<Model>("Asset/BuildIn/Model/material_ball/material_ball.fbx", processSetting);

        Vec3 offsetZ = Vec3(0, 0, 1) * 3;
        for(int i = 0; i < 5; i++)
        {
            std::shared_ptr<Entity> entity                                  = scene->CreateEntity("Material Ball[" + std::to_string(i) + "]");
            std::shared_ptr<MeshRendererComponent> meshRendererComponent    = entity->AddComponent<MeshRendererComponent>();
            std::shared_ptr<TransformComponent> transformComponent          = entity->TryGetComponent<TransformComponent>();

            transformComponent->SetPosition(Vec3(-28.0f, 1.0f, 2.0f) + i * offsetZ);
            transformComponent->SetRotation({0.0f, -90.0f, 0.0f});
            //transformComponent->SetScale({0.01f, 0.01f, 0.01f});

            MaterialRef material = materials[0 + i];

            meshRendererComponent->SetModel(model);
            meshRendererComponent->SetMaterial(material); 
        }
    }


    // EngineContext::Render()->SetPassEnabled(FORWARD_PASS, false);
    // EngineContext::Render()->SetPassEnabled(EDITOR_UI_PASS, false);
    // EngineContext::Render()->SetPassEnabled(IBL_PASS, false);
    // EngineContext::Render()->SetPassEnabled(SVGF_PASS, false);
    // EngineContext::Render()->SetPassEnabled(G_BUFFER_PASS, false);
    // EngineContext::Render()->SetPassEnabled(FORWARD_PASS, false);
    // EngineContext::Render()->SetPassEnabled(TRANSPARENT_PASS, false);
    // EngineContext::Render()->SetPassEnabled(DDGI_VISUALIZE_PASS, false);
    // EngineContext::Render()->SetPassEnabled(PATH_TRACING_PASS, false);
    // EngineContext::Render()->SetPassEnabled(BLOOM_PASS, false);
    // EngineContext::Render()->SetPassEnabled(FXAA_PASS, false);
    // EngineContext::Render()->SetPassEnabled(TAA_PASS, false);
    // EngineContext::Render()->SetPassEnabled(SSSR_PASS, false);
    // EngineContext::Render()->SetPassEnabled(VOLUMETIRC_FOG_PASS, false);
    // EngineContext::Render()->SetPassEnabled(RESTIR_DI_PASS, false);
    EngineContext::World()->SetActiveScene("defaultScene");
}

int main() 
{  
    EngineContext::Init();

    //InitCornellBoxScene();
    InitScene();
    EngineContext::MainLoop();
    EngineContext::Destroy();
    return 0; 
}



