#include "RHIStructs.h"
#include "RHIResource.h"
#include "Core/Log/Log.h"

#include <algorithm>

RHIRootSignatureInfo& RHIRootSignatureInfo::AddEntry(const ShaderResourceEntry& entry) 
{ 
    // 合并已有的 
    for (ShaderResourceEntry& oldEntry : entries)
    {
        if( oldEntry.set == entry.set && 
            oldEntry.binding == entry.binding)
        {
            if (oldEntry.type != entry.type) 
            {
                LOG_FATAL("Conflict shader resource entry!");
                return *this;
            }
            else 
            {
                if((oldEntry.size == 0 || entry.size == 0) && (oldEntry.size == 1 || entry.size == 1))  oldEntry.size = 0;
                else oldEntry.size = std::max(oldEntry.size, entry.size);        //反射得到的bindless数组的数量是0，把0当最大值吧
                                    
                oldEntry.frequency |= entry.frequency;
                return *this;
            }
        }
    }

    // 新加入
    entries.push_back(entry); 
    return *this; 
}

RHIRootSignatureInfo& RHIRootSignatureInfo::AddEntry(const RHIRootSignatureInfo& other)
{
    // 只合并entries（布局拼装）。★静态采样器表不随AddEntry传播（2026-10-24 OOM定案）：
    // 拼装=借布局（pass RS拼全局set1/set2布局），值绑定的所有权属于声明它的RS本身；
    // 若随拼装合并，"静态采样器声明的set无条件领用"会让每个拼装RS按自己的entries键
    // （set2差异⇒互不相同）各分配一个bindless布局巨型set1⇒显存爆炸（VK_ERROR_OUT_OF_DEVICE_MEMORY）
    for(auto& entry : other.entries) AddEntry(entry);
    return *this;
}

RHIRootSignatureInfo& RHIRootSignatureInfo::AddEntryFromReflect(RHIShaderRef shader) 
{   
    ShaderReflectInfo reflectInfo = shader->GetReflectInfo();

    for(ShaderResourceEntry& reflectEntry : reflectInfo.resources) AddEntry(reflectEntry);
    return *this; 
}
