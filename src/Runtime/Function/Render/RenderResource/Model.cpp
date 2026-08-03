
#include "Function/Render/RenderResource/Model.h"
#include "Core/Log/log.h"
#include "Core/Math/BoundingBox.h"
#include "Core/Math/Math.h"
#include "Core/Mesh/Mesh.h"
#include "Core/Mesh/TangentSpace.h"
#include "Core/Mesh/MeshOptimizor/MeshOptimizor.h"
#include "Function/Global/EngineContext.h"
#include "Function/Global/EngineThreadPool.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/Buffer.h"
#include "Function/Render/RenderResource/Texture.h"
#include "Material.h"
#include "RenderStructs.h"
#include "Resource/Asset/Asset.h"
#include "assimp/defs.h"
#include "assimp/types.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

CEREAL_REGISTER_TYPE(Model)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, Model)

CEREAL_REGISTER_TYPE(ModelCache)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, ModelCache)

Model::~Model()
{
    if (!EngineContext::Destroyed()) {
        for(auto& submesh : submeshes)
        {
            if(submesh.meshClusterID.begin != 0)        EngineContext::RenderResource()->ReleaseMeshClusterID(submesh.meshClusterID);
            if(submesh.meshClusterGroupID.begin != 0)   EngineContext::RenderResource()->ReleaseMeshClusterGroupID(submesh.meshClusterGroupID);
        }
    }
}

void Model::OnLoadAsset()
{
    BeginLoadAssetBind()
    LoadAssetBind(ModelCache, cache)
    ResizeAssetArray(materials)
    LoadAssetArrayBind(Material, materials)
    EndLoadAssetBind

    LoadFromFile(path);

    // 在读取模型数据后，分配GPU端的全部资源
    for(uint32_t i = 0; i < submeshes.size(); i++)
    {
        VertexBufferRef vertexBuffer = std::make_shared<VertexBuffer>();
        vertexBuffer->SetPosition(submeshes[i].mesh->position);
        vertexBuffer->SetNormal(submeshes[i].mesh->normal);
        vertexBuffer->SetTangent(submeshes[i].mesh->tangent);
        vertexBuffer->SetTexCoord(submeshes[i].mesh->texCoord);
        vertexBuffer->SetColor(submeshes[i].mesh->color);
        vertexBuffer->SetBoneIndex(submeshes[i].mesh->boneIndex);
        vertexBuffer->SetBoneWeight(submeshes[i].mesh->boneWeight);
        submeshes[i].vertexBuffer = vertexBuffer;

        IndexBufferRef indexBuffer = std::make_shared<IndexBuffer>();
        indexBuffer->SetIndex(submeshes[i].mesh->index);
        submeshes[i].indexBuffer = indexBuffer;
    }

    if(processSetting.generateCluster)
    {
        for(uint32_t i = 0; i < submeshes.size(); i++)
        {
            auto& submesh = submeshes[i];

            uint32_t indexOffset = 0;
            Mesh tempMesh = Mesh();             // 把单个submesh的全部cluster合并到一个buffer里存储
            std::vector<MeshClusterInfo> meshClusterInfos;
            VertexBufferRef vertexBuffer = std::make_shared<VertexBuffer>();
            IndexBufferRef indexBuffer = std::make_shared<IndexBuffer>();

            for(auto& cluster : submesh.clusters)  // cluster信息
            {
                tempMesh.Merge(*cluster->mesh.get());

                meshClusterInfos.push_back({
                    .vertexID = vertexBuffer->vertexID,
                    .indexID = indexBuffer->indexID,
                    .indexOffset = indexOffset,
                    .lodError = cluster->lodError,
                    .sphere = cluster->sphereBound
                });
                indexOffset += cluster->mesh->index.size();
            }
   
            vertexBuffer->SetPosition(tempMesh.position);
            vertexBuffer->SetNormal(tempMesh.normal);
            vertexBuffer->SetTangent(tempMesh.tangent);
            vertexBuffer->SetTexCoord(tempMesh.texCoord);
            vertexBuffer->SetColor(tempMesh.color);
            // vertexBuffer->SetBoneIndex(tempMesh.boneIndex);      // 不支持骨骼
            // vertexBuffer->SetBoneWeight(tempMesh.boneWeight);
            submesh.vertexBuffer = vertexBuffer;
  
            indexBuffer->SetIndex(tempMesh.index);
            submesh.indexBuffer = indexBuffer;

            submesh.meshClusterID = EngineContext::RenderResource()->AllocateMeshClusterID(meshClusterInfos.size());
            EngineContext::RenderResource()->SetMeshClusterInfo(meshClusterInfos, submesh.meshClusterID.begin);
        }
    }

    std::vector<uint32_t> lod0TriangleNums(submeshes.size());   // 对于虚拟几何体，构建加速结构使用的是第0层cluster的全部三角形
    if(processSetting.generateVirtualMesh)
    {   
        for(uint32_t i = 0; i < submeshes.size(); i++)
        {
            auto& submesh = submeshes[i];

            uint32_t indexOffset = 0;
            Mesh tempMesh = Mesh();             // 把单个submesh的全部cluster合并到一个buffer里存储
            std::vector<MeshClusterInfo> meshClusterInfos;
            std::vector<MeshClusterGroupInfo> meshClusterGroupInfos;
            VertexBufferRef vertexBuffer = std::make_shared<VertexBuffer>();
            IndexBufferRef indexBuffer = std::make_shared<IndexBuffer>();

            lod0TriangleNums[i] = 0;
            for(auto& cluster : submesh.virtualMesh->clusters)  // cluster信息
            {
                tempMesh.Merge(*cluster->mesh.get());

                meshClusterInfos.push_back({
                    .vertexID = vertexBuffer->vertexID,
                    .indexID = indexBuffer->indexID,
                    .indexOffset = indexOffset,
                    .lodError = cluster->lodError,
                    .sphere = cluster->sphereBound
                });
                indexOffset += cluster->mesh->index.size();

                if(cluster->mipLevel == 0) lod0TriangleNums[i] += cluster->mesh->TriangleNum();
            }

            vertexBuffer->SetPosition(tempMesh.position);
            vertexBuffer->SetNormal(tempMesh.normal);
            vertexBuffer->SetTangent(tempMesh.tangent);
            vertexBuffer->SetTexCoord(tempMesh.texCoord);
            vertexBuffer->SetColor(tempMesh.color);
            // vertexBuffer->SetBoneIndex(tempMesh.boneIndex);      // 不支持骨骼
            // vertexBuffer->SetBoneWeight(tempMesh.boneWeight);
            submesh.vertexBuffer = vertexBuffer;
  
            indexBuffer->SetIndex(tempMesh.index);
            submesh.indexBuffer = indexBuffer;

            submesh.meshClusterID = EngineContext::RenderResource()->AllocateMeshClusterID(meshClusterInfos.size());
            EngineContext::RenderResource()->SetMeshClusterInfo(meshClusterInfos, submesh.meshClusterID.begin);

            for(auto& clusterGroup : submesh.virtualMesh->clusterGroups)    // cluster group信息
            {
                MeshClusterGroupInfo groupInfo = {};
                for(uint32_t j = 0; j < clusterGroup->clusters.size(); j++) groupInfo.clusterID[j] = clusterGroup->clusters[j] + submesh.meshClusterID.begin;  // 本地偏移加全局偏移
                groupInfo.clusterSize = clusterGroup->clusters.size();
                groupInfo.parentLodError = clusterGroup->parentLodError;
                groupInfo.mipLevel = clusterGroup->mipLevel;
                groupInfo.sphere = clusterGroup->lodBound;

                meshClusterGroupInfos.push_back(groupInfo);
            }

            submesh.meshClusterGroupID = EngineContext::RenderResource()->AllocateMeshClusterGroupID(meshClusterGroupInfos.size());
            EngineContext::RenderResource()->SetMeshClusterGroupInfo(meshClusterGroupInfos, submesh.meshClusterGroupID.begin);
        }
    }

#if ENABLE_RAY_TRACING
    for(uint32_t i = 0; i < submeshes.size(); i++)
    {
        RHIBottomLevelAccelerationStructureInfo blasInfo = {};
        blasInfo.vertexBuffer = submeshes[i].vertexBuffer->positionBuffer;
        blasInfo.indexBuffer = submeshes[i].indexBuffer->buffer;
        blasInfo.triangleNum =  (processSetting.generateVirtualMesh) ?  lod0TriangleNums[i] :                       // 虚拟几何体，用第0层cluster
                                (processSetting.generateCluster) ?      submeshes[i].indexBuffer->TriangleNum() :   // 分簇，用全部cluster
                                                                        submeshes[i].indexBuffer->TriangleNum();    // 默认
        // blasInfo.triangleNum = submeshes[i].indexBuffer->TriangleNum();    // 默认
        blasInfo.vertexStride = sizeof(Vec3);
        blasInfo.indexOffset = 0;
        blasInfo.vertexOffset = 0;

        submeshes[i].blas = EngineContext::RHI()->CreateBottomLevelAccelerationStructure(blasInfo);
    }
#endif

}

void Model::OnSaveAsset()
{
    if(processSetting.cacheCluster)
    {
        if(!cache) cache = std::make_shared<ModelCache>();
        cache->submeshes.clear();
        cache->submeshes.resize(submeshes.size());
        for(uint32_t i = 0; i < submeshes.size(); i++)
        {
            cache->submeshes[i].clusters = submeshes[i].clusters;
            cache->submeshes[i].virtualMesh = submeshes[i].virtualMesh;
        }
        if(EngineContext::Asset()->UIDToFilePath(cache->GetUID()).empty())    // 路径为空说明cache还没有保存到文件过，手动保存一下
        {
            std::string modelPath = EngineContext::Asset()->UIDToFilePath(this->GetUID());
            assert(!modelPath.empty());

            //ENGINE_LOG_INFO("Saving model cache {} ...", EngineContext::File()->ReplaceExtension(modelPath, ".modelCache.binasset"));
            EngineContext::Asset()->SaveAsset(cache, EngineContext::File()->ReplaceExtension(modelPath, ".modelCache.binasset"));
        }   
    }

    BeginSaveAssetBind()
    SaveAssetBind(cache)
    SaveAssetArrayBind(materials)
    EndSaveAssetBind
}

Model::Model(std::string path, ModelProcessSetting processSetting)
: path(path)
, processSetting(processSetting)
{
    OnLoadAsset();
}

bool Model::LoadFromFile(std::string path)
{
    //if (processSetting.generateVirtualMesh) processSetting.smoothNormal = true;  //对于生成虚拟几何体需要顶点去重，强制平滑法线

    uint32_t processSteps = aiProcess_Triangulate;  // | aiProcess_FixInfacingNormals;  // 对单面物体有BUG?
    if (processSetting.flipUV) processSteps |= aiProcess_FlipUVs;
    if (processSetting.smoothNormal) processSteps |= aiProcess_DropNormals | aiProcess_GenSmoothNormals;
    if (!processSetting.smoothNormal) processSteps |= aiProcess_JoinIdenticalVertices | aiProcess_GenNormals;  //不需要平滑法线就可以合并重复顶点了，

    Assimp::Importer import;
    const aiScene* scene = import.ReadFile(EngineContext::File()->Absolute(path), processSteps);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        ENGINE_LOG_INFO("Assimp load error : {}", import.GetErrorString());
        return false;
    }

    std::vector<aiMesh*> processMeshes = {};
    //submeshes.resize(processMeshes.size());
    submeshes.clear();
    ProcessNode(scene->mRootNode, scene, processMeshes, aiMatrix4x4());
    
    if(processSetting.loadMaterials) materials.resize(processMeshes.size());

    //并行加载各个子mesh
    for(int i = 0; i < processMeshes.size(); i++)
    {
        aiMesh* mesh = processMeshes[i];
        ENGINE_LOG_INFO("[{}/{}] Start processing mesh [{}]. ", i, processMeshes.size(), mesh->mName.C_Str());   // scene->mNumMeshes ?

        ProcessMesh(mesh, scene, i);

        // TODO 并行加载
        // EngineContext::ThreadPool()->AddQueuedWork([mesh, scene, i, this](){
        //     ProcessMesh(mesh, scene, i);
        // });
    }  
    // EngineContext::ThreadPool()->WaitAllIdle();
    textureMap.clear();

    // 统计信息
    totalIndex = 0;
    totalVertex = 0;
    for(auto& submesh : submeshes)
    {
        totalIndex += submesh.mesh->index.size();
        totalVertex += submesh.mesh->position.size();
    }
    if(processSetting.generateCluster)
    {
        totalClusterCnt = 0;
        for(auto& submesh : submeshes)
        {   
            totalClusterCnt += submesh.clusters.size();
        }
        ENGINE_LOG_INFO("Mesh cluster build success. total clusters: {}", totalClusterCnt);
    }
    if(processSetting.generateVirtualMesh)
    {
        totalClusterCnt = 0;
        totalClusterMaxMip = 0;
        for(auto& submesh : submeshes)
        {   
            for(auto& clusterGroup : submesh.virtualMesh->clusterGroups)
            {
                totalClusterMaxMip = std::max(totalClusterMaxMip, clusterGroup->mipLevel);
            }
            totalClusterCnt += submesh.virtualMesh->clusters.size();
        }
        ENGINE_LOG_INFO("Virtual mesh build success. total clusters: {}, max miplevel: {}", totalClusterCnt, totalClusterMaxMip);
    }

    return true;
}

void Model::ProcessNode(aiNode* node, const aiScene* scene, std::vector<aiMesh*>& processMeshes, aiMatrix4x4 mat)
{
    aiMatrix4x4 newMat = mat * node->mTransformation;
    Mat4 newTransform = ProcessTransform(newMat);

    // 处理节点所有的网格（如果有的话）
    for (uint32_t i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        
        processMeshes.push_back(mesh);
        submeshes.emplace_back();
        submeshes.back().transform = newTransform;
        submeshes.back().scale = Math::GetScale(newTransform);
    }
    // 接下来对它的子节点重复这一过程
    for (uint32_t i = 0; i < node->mNumChildren; i++)
    {
        ProcessNode(node->mChildren[i], scene, processMeshes, newMat);
    }
}

Mat4 Model::ProcessTransform(aiMatrix4x4 mat)
{
    Mat4 newMat;
    newMat(0, 0) = mat.a1;
    newMat(0, 1) = mat.a2;
    newMat(0, 2) = mat.a3;
    newMat(0, 3) = mat.a4;
    newMat(1, 0) = mat.b1;
    newMat(1, 1) = mat.b2;
    newMat(1, 2) = mat.b3;
    newMat(1, 3) = mat.b4;
    newMat(2, 0) = mat.c1;
    newMat(2, 1) = mat.c2;
    newMat(2, 2) = mat.c3;
    newMat(2, 3) = mat.c4;
    newMat(3, 0) = mat.d1;
    newMat(3, 1) = mat.d2;
    newMat(3, 2) = mat.d3;
    newMat(3, 3) = mat.d4;
    //newMat.transposeInPlace();

    return newMat;
}

void Model::ProcessMesh(aiMesh* mesh, const aiScene* scene, int index)
{
    std::shared_ptr<Mesh> submesh = std::make_shared<Mesh>();

    // 处理顶点位置
    submesh->position = std::vector<Vec3>(mesh->mNumVertices);
    for (uint32_t i = 0; i < mesh->mNumVertices; i++)
    {         
        submesh->position[i](0) = mesh->mVertices[i].x;
        submesh->position[i](1) = mesh->mVertices[i].y;
        submesh->position[i](2) = mesh->mVertices[i].z;       
    }

    // 处理顶点法线
    if (mesh->mNormals) 
    {
        submesh->normal = std::vector<Vec3>(mesh->mNumVertices);
        for (uint32_t i = 0; i < mesh->mNumVertices; i++)
        {  
            submesh->normal[i](0) = mesh->mNormals[i].x;
            submesh->normal[i](1) = mesh->mNormals[i].y;
            submesh->normal[i](2) = mesh->mNormals[i].z;
        }
    }

    // 处理顶点颜色
    if (mesh->mColors[0])
    {
        submesh->color = std::vector<Vec3>(mesh->mNumVertices);
        for (uint32_t i = 0; i < mesh->mNumVertices; i++)
        {  
            submesh->color[i](0) = mesh->mColors[0][i].r;
            submesh->color[i](1) = mesh->mColors[0][i].g;
            submesh->color[i](2) = mesh->mColors[0][i].b;
        }
    }
        
    // 处理顶点纹理坐标
    if (mesh->mTextureCoords[0])
    {
        submesh->texCoord = std::vector<Vec2>(mesh->mNumVertices);
        for (uint32_t i = 0; i < mesh->mNumVertices; i++)
        {  
            submesh->texCoord[i](0) = mesh->mTextureCoords[0][i].x;
            submesh->texCoord[i](1) = mesh->mTextureCoords[0][i].y;   
        }
    }
    // if (mesh->mTextureCoords[1])
    // {
    //     submesh->texCoord = std::vector<Vec2>(mesh->mNumVertices);
    //     for (uint32_t i = 0; i < mesh->mNumVertices; i++)
    //     {  
    //         submesh->texCoord[i](0) = mesh->mTextureCoords[1][i].x;
    //         submesh->texCoord[i](1) = mesh->mTextureCoords[1][i].y;    
    //     }
    // }

    // 处理索引,已经三角面化了就全当3处理了
    submesh->index = std::vector<uint32_t>(mesh->mNumFaces * 3);

    int tempCnt = 0;
    for (uint32_t i = 0; i < mesh->mNumFaces; i++)
    {
        aiFace face = mesh->mFaces[i];
        for (uint32_t j = 0; j < face.mNumIndices; j++)
        {
            submesh->index[j + tempCnt] = face.mIndices[j];
        }
        tempCnt += face.mNumIndices;
    }

    // 处理顶点切线
    if (mesh->mTangents)
    {
        submesh->tangent = std::vector<Vec4>(mesh->mNumVertices);
        for (uint32_t i = 0; i < mesh->mNumVertices; i++)
        {  
            submesh->tangent[i](0) = mesh->mTangents[i].x;
            submesh->tangent[i](1) = mesh->mTangents[i].y;
            submesh->tangent[i](2) = mesh->mTangents[i].z;
            submesh->tangent[i](3) = 1.0f;  //最后一位为符号(手性)
        }
    }
    else if (processSetting.tangentSpace)   
    {
        if( submesh->normal.size() == 0 ||
            submesh->position.size() == 0 ||
            submesh->texCoord.size() == 0)      // 必须要有这些数据才能生成
        {
            ENGINE_LOG_INFO("Try to generate tangent space but missing necesscary datas!");
        }
        else 
        {
            submesh->tangent = std::vector<Vec4>(mesh->mNumVertices);

            TangentSpace tangentCalculator = TangentSpace();
            tangentCalculator.Generate(submesh.get());  // 需要先把上面的信息准备完成
        }     
    }

    // 处理材质
    if (processSetting.loadMaterials && mesh->mMaterialIndex >= 0)
    {
        aiMaterial* aiMaterial = scene->mMaterials[mesh->mMaterialIndex];

        if(materials[index] == nullptr) // 首次创建；后续通过序列化创建时会绑定第一次创建的材质
        {
            materials[index] = std::make_shared<Material>();
            std::shared_ptr<Texture> diffuse = LoadMaterialTexture(aiMaterial, aiTextureType_DIFFUSE);
            std::shared_ptr<Texture> normal = LoadMaterialTexture(aiMaterial, aiTextureType_NORMALS);
            std::shared_ptr<Texture> specular = LoadMaterialTexture(aiMaterial, aiTextureType_SPECULAR);
            //std::shared_ptr<Texture> unknownTexture = LoadMaterialTexture(aiMaterial, aiTextureType_UNKNOWN);

            materials[index]->SetDiffuse(diffuse);
            materials[index]->SetNormal(normal);
            //materials[index]->SetSpecular(specular);

            materials[index]->SetARM(specular); // ? TODO

            // aiColor4D color4;
            // Vec4 vec4;
            // aiMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, color4);
            // vec4 = Vec4(color4.r, color4.g, color4.b, color4.a);
            // if(!color4.IsBlack()) materials[index]->SetDiffuse(vec4);
            // ENGINE_LOG_INFO("AI_MATKEY_COLOR_DIFFUSE [{} {} {} {}]", color4.r, color4.g, color4.b, color4.a);

            // aiShadingMode mode = aiShadingMode_Flat;
            // aiMaterial->Get(AI_MATKEY_SHADING_MODEL, mode);
            // ENGINE_LOG_INFO("AI_MATKEY_SHADING_MODEL [{}]", (int)mode);

            // ENGINE_LOG_INFO("AI_MATKEY_METALLIC_FACTOR [{}]", data);
            // aiMaterial->mProperties[0]->mKey.C_Str();


            // for(int i = 0; i < aiMaterial->mNumProperties; i++)
            // {
            //     ENGINE_LOG_INFO("[{}]", aiMaterial->mProperties[i]->mKey.C_Str());
            // }



            // TODO 并行加载
            // materials[index] = std::make_shared<Material>();
            // EngineContext::ThreadPool()->AddQueuedWork([this, index, aiMaterial](){
            //     std::shared_ptr<Texture> diffuse = LoadMaterialTexture(aiMaterial, aiTextureType_DIFFUSE);
            //     materials[index]->SetDiffuse(diffuse);
            // }, ENGINE_THREAD_TYPE_RHI);
            // EngineContext::ThreadPool()->AddQueuedWork([this, index, aiMaterial](){
            //     std::shared_ptr<Texture> normal = LoadMaterialTexture(aiMaterial, aiTextureType_NORMALS);
            //     materials[index]->SetNormal(normal);
            // }, ENGINE_THREAD_TYPE_RHI);
            // EngineContext::ThreadPool()->AddQueuedWork([this, index, aiMaterial](){
            //     std::shared_ptr<Texture> specular = LoadMaterialTexture(aiMaterial, aiTextureType_SPECULAR);
            //     materials[index]->SetSpecular(specular);
            // }, ENGINE_THREAD_TYPE_RHI);
        }
    }

    // 处理骨骼
    if (mesh->HasBones())   ExtractBoneWeights(submesh.get(), mesh, scene);

    // 处理包围盒
    submesh->aabb = AxisAlignedBox(submesh->position[0], Vec3::Zero());
    for (uint32_t i = 0; i < mesh->mNumVertices; i++)   submesh->aabb.Merge(submesh->position[i]);
    submesh->sphere = BoundingSphere(submesh->aabb);
    submesh->box = BoundingBox(submesh->aabb);

    // 处理mesh名称
    submesh->name = std::string(mesh->mName.C_Str());

    // 优化缓存
    // MeshOptimizor::OptimizeMesh(submesh);

    // 添加到mesh asset
    submeshes[index].mesh = submesh;

    // 处理分簇
    if (processSetting.generateCluster)
    {
        if( processSetting.cacheCluster &&
            cache && 
            cache->submeshes.size() > index && 
            cache->submeshes[index].clusters.size() > 0) //使用缓存
        {
            ENGINE_LOG_INFO("Loading mesh cluster from cache...");
            submeshes[index].clusters = cache->submeshes[index].clusters;
        }
        else 
        {
            ClusterTriangles(submeshes[index].mesh, submeshes[index].clusters);
        }

        for (auto& cluster : submeshes[index].clusters) cluster->FixSize();
    }

    // 处理虚拟几何体
    if(processSetting.generateVirtualMesh)
    {
        if( processSetting.cacheCluster && 
            cache && 
            cache->submeshes.size() > index && 
            cache->submeshes[index].virtualMesh) //使用缓存
        {
            ENGINE_LOG_INFO("Loading virtual mesh from cache...");
            submeshes[index].virtualMesh = cache->submeshes[index].virtualMesh;
        }
        else 
        {
            // submeshes[index].mesh->normal.clear();
            // submeshes[index].mesh->tangent.clear();
            // submeshes[index].mesh->texCoord.clear();
            // submeshes[index].mesh->color.clear();
            // submeshes[index].mesh->boneIndex.clear();
            // submeshes[index].mesh->boneWeight.clear();

            submeshes[index].virtualMesh = std::make_shared<VirtualMesh>();
            submeshes[index].virtualMesh->Build(submeshes[index].mesh);
        }
    }
}

void Model::ExtractBoneWeights(Mesh* submesh, aiMesh* mesh, const aiScene* scene)
{
    submesh->boneIndex = std::vector<IVec4>(mesh->mNumVertices);
    submesh->boneWeight = std::vector<Vec4>(mesh->mNumVertices);

    // 将骨骼相关信息初始化
    for(int i = 0; i < mesh->mNumVertices; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            submesh->boneIndex[i](j) = -1;
            submesh->boneWeight[i](j) = 0.0f;
        }
    }

    // 遍历骨骼
    for (uint32_t index = 0; index < mesh->mNumBones; ++index)
    {
        int boneIndex = -1;
        std::string boneName = mesh->mBones[index]->mName.C_Str();

        bool find = false;
        for(int i = 0; i < submesh->bone.size(); i++)
        {
            if(submesh->bone[i].name.compare(boneName) == 0)
            {
                boneIndex = submesh->bone[i].index;
                find = true;
                break;
            }
        }
        if (!find)
        {
            BoneInfo newBoneInfo;
            newBoneInfo.index = (int)submesh->bone.size();
            newBoneInfo.name = boneName;
            for (int i = 0; i < 4; i++)
            {
                for (int j = 0; j < 4; j++)
                {
                    newBoneInfo.offset(i, j) = mesh->mBones[index]->mOffsetMatrix[i][j];
                }
            }
            newBoneInfo.offset.transposeInPlace();  // 要做一个转置？
            newBoneInfo.name = std::string(boneName);
            submesh->bone.push_back(newBoneInfo);

            boneIndex = newBoneInfo.index;
        }

        auto weights = mesh->mBones[index]->mWeights;
        int numWeights = mesh->mBones[index]->mNumWeights;

        // 处理和该骨骼相关的顶点
        for (int weightIndex = 0; weightIndex < numWeights; ++weightIndex)
        {
            int vertexId = weights[weightIndex].mVertexId;
            float weight = weights[weightIndex].mWeight;

            for (int i = 0; i < 4; ++i)
            {
                if(submesh->boneIndex[vertexId](i) < 0)
                {
                    submesh->boneIndex[vertexId](i) = boneIndex;
                    submesh->boneWeight[vertexId](i) = weight;
                    break;
                }
            }
        }
    }
}

std::shared_ptr<Texture> Model::LoadMaterialTexture(aiMaterial* mat, aiTextureType type)
{
    for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)   //可以有很多个，只用了一个
    {
        aiString str;
        mat->GetTexture(type, i, &str);

        std::string texturePath = EngineContext::File()->RemoveFilename(path).append("/").append(str.C_Str());

        if(processSetting.forcePngTexture)
            texturePath = EngineContext::File()->ReplaceExtension(texturePath, "png");

        auto iter = textureMap.find(texturePath);   // 先从缓存中找
        if(iter != textureMap.end())    return iter->second;
        else
        {
            ENGINE_LOG_INFO("Loading texture [{}]...", texturePath);
            std::shared_ptr<Texture> texture = std::make_shared<Texture>(texturePath);
            // EngineContext::Asset()->SaveAsset(texture);
            textureMap[texturePath] = texture;
            return texture;
        }
    }
    return nullptr;
}