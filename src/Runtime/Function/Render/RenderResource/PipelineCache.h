#pragma once

#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RHI/RHIResource.h"

#include "MurmurHash2.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

class GraphicsPipelineCache
{
public:
    struct CachedPipeline
    {
        RHIGraphicsPipelineRef pipeline;
    };

    struct Key
    {
        Key(const RHIGraphicsPipelineInfo& info) 
        // : vertexShader(info.vertexShader)
        // , geometryShader(info.geometryShader)
        // , fragmentShader(info.fragmentShader)
        // , rootSignature(info.rootSignature)
        // , vertexInputState(info.vertexInputState)
        // , primitiveType(info.primitiveType)
        // , rasterizerState(info.rasterizerState)
        // , blendState(info.blendState)
        // , depthStencilState(info.depthStencilState)
        // , colorAttachmentFormats(info.colorAttachmentFormats)
        // , depthStencilAttachmentFormat(info.depthStencilAttachmentFormat)
        : info(info)
        {}
  
        // RHIShaderRef					    vertexShader;
        // RHIShaderRef					    geometryShader;
        // RHIShaderRef	 				    fragmentShader;

        // RHIRootSignatureRef				rootSignature;

        // VertexInputStateInfo             vertexInputState;
        // PrimitiveType					primitiveType;
        // RHIRasterizerStateInfo			rasterizerState;
        // RHIBlendStateInfo				blendState;
        // RHIDepthStencilStateInfo		    depthStencilState;

        // std::array<RHIFormat, MAX_RENDER_TARGETS> colorAttachmentFormats;
        // RHIFormat depthStencilAttachmentFormat;

        RHIGraphicsPipelineInfo info;

        friend bool operator== (const Key& a, const Key& b)
        {
            // return  a.vertexShader.get() == b.vertexShader.get() &&
            //         a.geometryShader.get() == b.geometryShader.get() &&
            //         a.fragmentShader.get() == b.fragmentShader.get() &&
            //         a.rootSignature.get() == b.rootSignature.get() &&
            //         a.vertexInputState == b.vertexInputState &&
            //         a.primitiveType == b.primitiveType &&
            //         a.rasterizerState == b.rasterizerState &&
            //         a.blendState == b.blendState &&
            //         a.depthStencilState == b.depthStencilState;
            return a.info == b.info;
        }
        
        // MurmurHash64A是针对POD做内存hash，在这个过程中info的shared_ptr和vertex都不可以参与
        struct Hash {
            struct Flat {
                void* vs; void* gs; void* fs; void* rs;
                // VertexInputStateInfo            vertexInputState;
                PrimitiveType                   primitiveType;
                RHIRasterizerStateInfo          rasterizerState;
                RHIBlendStateInfo               blendState;
                RHIDepthStencilStateInfo        depthStencilState;
                std::array<RHIFormat, MAX_RENDER_TARGETS> colorAttachmentFormats;
                RHIFormat                       depthStencilAttachmentFormat;
                uint32_t                        viewMask;
            };
            size_t operator()(const Key& a) const {
                const RHIGraphicsPipelineInfo& i = a.info;
                Flat flat = {
                      i.vertexShader.get(), i.geometryShader.get(),
                      i.fragmentShader.get(), i.rootSignature.get(),
                      // i.vertexInputState, 
                      i.primitiveType, i.rasterizerState,
                      i.blendState, i.depthStencilState,
                      i.colorAttachmentFormats, i.depthStencilAttachmentFormat, i.viewMask
                };
                return  MurmurHash64A(&flat, sizeof(Flat), 0);
            }
        };
    };

    CachedPipeline Allocate(const RHIGraphicsPipelineInfo& info);

    inline uint32_t CachedSize()    { return cachedPipelines.size(); }
    void Clear()                    { cachedPipelines.clear(); }

    static std::shared_ptr<GraphicsPipelineCache> Get()
    {
        static std::shared_ptr<GraphicsPipelineCache> pool;
        if(pool == nullptr) pool = std::make_shared<GraphicsPipelineCache>();
        return pool;
    }

private:
    std::unordered_map<Key, CachedPipeline, Key::Hash> cachedPipelines;   

    bool IsValid(RHIGraphicsPipelineInfo info);
};