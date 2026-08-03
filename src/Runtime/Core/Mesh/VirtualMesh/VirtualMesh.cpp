#pragma once

#include "VirtualMesh.h"
#include "Core/Log/Log.h"

#include <algorithm>
#include <cstdint>

void LogClusterSize(std::vector<MeshClusterRef>& clusters, uint32_t begin, uint32_t end) 
{
    float maxsz = 0, minsz = 100000, avgsz = 0;
    for (uint32_t i = begin; i < end; i++) 
    {
        auto& cluster = clusters[i];
        //assert(cluster->verts.size() < 256);

        float sz = cluster->mesh->index.size() / 3.0;
        if (sz > maxsz) maxsz = sz;
        if (sz < minsz) minsz = sz;
        avgsz += sz;
    }
    avgsz /= end - begin;
    LOG_DEBUG("cluster size: max=%f, min=%f, avg=%f\n", maxsz, minsz, avgsz);
}

void LogGroupSize(std::vector<std::shared_ptr<MeshClusterGroup>>& groups, uint32_t begin, uint32_t end)
{
    float maxsz = 0, minsz = 100000, avgsz = 0;
    for (uint32_t i = begin; i < end; i++) 
    {
        float sz = groups[i]->clusters.size();

        if (sz > maxsz) maxsz = sz;
        if (sz < minsz) minsz = sz;
        avgsz += sz;
    }
    avgsz /= end - begin;
    LOG_DEBUG("groups size: max=%f, min=%f, avg=%f\n", maxsz, minsz, avgsz);
}

void VirtualMesh::Build(MeshRef mesh) 
{
    ClusterTriangles(mesh, clusters);

    uint32_t originTriangles = mesh->TriangleNum();
    uint32_t levelOffset = 0;  
    uint32_t mipLevel = 0;
    uint32_t prevGroupNum = 0;

    LOG_DEBUG("VirtualMesh::Build origin triangles [%d]\n", originTriangles);
    while (true) 
    {
        uint32_t triangles = 0;
        for(int i = levelOffset; i < clusters.size(); i++)
            triangles += clusters[i]->mesh->TriangleNum();
        uint32_t groups = clusterGroups.size() - prevGroupNum;

        LOG_DEBUG("VirtualMesh::Build generate level [%d] clusters: [%d] [%d] [%d]\n", mipLevel, (uint32_t)clusters.size() - levelOffset, triangles, groups);

        //LogClusterSize(clusters, levelOffset, clusters.size());

        uint32_t numLevelClusters = clusters.size() - levelOffset;   
        if (numLevelClusters == 0) break; //上轮循环未生成新的cluster，终止循环

        uint32_t prevClusterNum = clusters.size();
        prevGroupNum = clusterGroups.size();
                                                                    //UE 还给了一个continue条件？？
        if (numLevelClusters <= MeshClusterGroup::GROUP_SIZE)       //当聚类数目小于group的最大cluster数目MeshClusterGroup::GROUP_SIZE时，可以直接合并成一个group了，                                                         
        {                                                           //group的边界边之类的信息也不需要收集
            auto group = std::make_shared<MeshClusterGroup>();
            clusterGroups.push_back(group);
            group->mipLevel = mipLevel;

            for (int i = 0; i < numLevelClusters; i++) group->clusters.push_back(levelOffset + i);

            if (numLevelClusters == 1)    //上轮循环生成的新cluster数目只有一个，终止循环（最后一轮也要建立group，再退出）
            {
                auto& cluster = clusters[group->clusters[0]];
                group->lodBound = cluster->lodBound;
                group->parentLodError = 1000;   //顶层的无效值

                break;
            }
        }
        else 
        {
            GroupClusters(
                clusters,
                levelOffset,
                numLevelClusters,
                clusterGroups,
                mipLevel);
        }
        
        //LogGroupSize(clusterGroups, prevGroupNum, clusterGroups.size());

        for (uint32_t i = prevGroupNum; i < clusterGroups.size(); i++) 
        {
            BuildParentClusters(clusterGroups[i], clusters);
        }

        levelOffset = prevClusterNum;
        mipLevel++;
    }
    numMipLevel = mipLevel + 1;

    for (auto& cluster : clusters) cluster->FixSize();

    //LOG_DEBUG("VirtualMesh::Build success. origin triangles: %d, total clusters: %d, total mip levels: %d\n", originTriangles, (uint32_t)clusters.size(), numMipLevel);
}