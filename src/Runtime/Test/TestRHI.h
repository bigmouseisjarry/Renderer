// #pragma once

// #include "Function/Framework/Component/TransformComponent.h"
// #include "Function/Framework/Scene/Scene.h"
// #include "Function/Global/EngineContext.h"
// #include "Function/Render/RenderResource/Buffer.h"
// #include "Function/Render/RenderResource/Model.h"
// #include "Function/Render/RenderResource/Texture.h"
// #include "TestMath.h"

//#include <algorithm>
//#include <cmath>
//#include <unordered_map>
//#include <unordered_set>
//#include <ranges>
//#include <atomic>

// #include "GLFW/glfw3.h"

// extern Extent2D windowsExtent;
// extern Offset2D windowsOffset;
// extern RHIFormat colorFormat;
// extern RHIFormat depthFormat;
// extern uint32_t framesInFlight;
// extern std::string shaderPath;

// /*
// typedef struct Vertex
// {
//     float pos_color[6]; 
// } Vertex;
// const Vertex vertices[] = {
//     {-0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f},
//     {0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f},
//     {0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f},

//     {-0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f},
//     {0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f},
//     {0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f},
// };
// const uint32_t indices[] = {
//     0, 1, 2,
//     3, 4, 5,
// };
// */

// static void TestRHI(GLFWwindow* window)
// {
//     std::shared_ptr<Scene> scene = EngineContext::World()->LoadScene("resource/build_in/config/scene/default.scene");
//     std::shared_ptr<Entity> e1 = scene->GetEntity("Entity1");
//     std::shared_ptr<Entity> e2 = scene->GetEntity("Entity2");

//     std::shared_ptr<TransformComponent> transformComponent = e1->TryGetComponent<TransformComponent>();
    
//     EngineContext::World()->SetActiveScene("defaultScene");

//     std::shared_ptr<Model> model    = EngineContext::Asset()->GetOrLoadAsset<Model>("resource/build_in/config/model/klee.model");
//     std::vector<TextureRef> textures;
//     textures.push_back(std::make_shared<Texture>("resource/build_in/model/Klee/Texture/face.jpg")); 
//     textures.push_back(std::make_shared<Texture>("resource/build_in/model/Klee/Texture/hair.jpg")); 
//     textures.push_back(EngineContext::Asset()->GetOrLoadAsset<Texture>("resource/build_in/config/texture/dressing.texr"));  

//     std::vector<uint8_t> vertShaderCode;
//     std::vector<uint8_t> fragShaderCode;
//     std::vector<uint8_t> compShaderCode;
//     EngineContext::File()->LoadBinary(shaderPath + "test/default.vert.spv", vertShaderCode);
//     EngineContext::File()->LoadBinary(shaderPath + "test/forward.frag.spv", fragShaderCode);
//     EngineContext::File()->LoadBinary(shaderPath + "test/deferred_lighting.comp.spv", compShaderCode);

//     // RHI //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//     RHIBackendRef backend = EngineContext::RHI();

//     // surface
//     RHISurfaceRef surface = backend->CreateSurface(window);

//     // queue
//     RHIQueueInfo queueInfo = { 
//         .type = QUEUE_TYPE_GRAPHICS, 
//         .index = 0 };
//     RHIQueueRef queue = backend->GetQueue(queueInfo);

//     // commandPool
//     RHICommandPoolInfo poolInfo = {
//         .queue = queue
//     };
//     RHICommandPoolRef pool = backend->CreateCommandPool(poolInfo);
//     std::vector<RHICommandListRef> commands;
//     for(uint32_t i = 0; i < framesInFlight; i++) commands.push_back(pool->CreateCommandList(false));

//     // swapchain
//     RHISwapchainInfo swapchainInfo = {
//         .surface = surface,
//         .presentQueue = queue,
//         .imageCount = framesInFlight,
//         .extent = surface->GetExetent(),
//         .format = colorFormat };
//     RHISwapchainRef swapchain = backend->CreateSwapChain(swapchainInfo);

//     // buffer
//     // RHIBufferInfo rwBufferInfo = {
//     //     .size = 100 * sizeof(float) * 6,
//     //     .memoryUsage = MEMORY_USAGE_CPU_TO_GPU,
//     //     .type = RESOURCE_TYPE_RW_BUFFER,
//     //     .creationFlag = BUFFER_CREATION_PERSISTENT_MAP
//     // };
//     // RHIBufferRef rwBuffer = backend->CreateBuffer(rwBufferInfo);


//     // RHIBufferInfo vertexBufferInfo = {
//     //     .size = 3 * sizeof(Vertex),
//     //     .memoryUsage = MEMORY_USAGE_CPU_TO_GPU,
//     //     .type = RESOURCE_TYPE_RW_BUFFER | RESOURCE_TYPE_VERTEX_BUFFER,
//     //     .creationFlag = BUFFER_CREATION_PERSISTENT_MAP
//     // };
//     // RHIBufferRef vertexBuffer = backend->CreateBuffer(vertexBufferInfo);
//     // memcpy(vertexBuffer->Map(), &vertices[0], 3 * sizeof(Vertex));

//     // RHIBufferInfo indexBufferInfo = {
//     //     .size = 3 * sizeof(uint32_t), 
//     //     .memoryUsage = MEMORY_USAGE_CPU_TO_GPU,
//     //     .type = RESOURCE_TYPE_RW_BUFFER | RESOURCE_TYPE_INDEX_BUFFER,
//     //     .creationFlag = BUFFER_CREATION_PERSISTENT_MAP
//     // };
//     // RHIBufferRef indexBuffer = backend->CreateBuffer(indexBufferInfo);
//     // memcpy(indexBuffer->Map(), &indices[0], 3 * sizeof(uint32_t));

//     // texture
//     RHITextureInfo colorTextureInfo = {
//         .format = colorFormat,
//         .extent = { windowsExtent.width, windowsExtent.height, 1},
//         .arrayLayers = 1,
//         .mipLevels = 1,
//         .memoryUsage = MEMORY_USAGE_GPU_ONLY,
//         .type = RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_RW_TEXTURE | RESOURCE_TYPE_RENDER_TARGET,
//         .creationFlag = TEXTURE_CREATION_NONE
//     };
//     RHITextureRef colorTexture = backend->CreateTexture(colorTextureInfo);


//     RHITextureInfo depthTextureInfo = {
//         .format = depthFormat,
//         .extent = { windowsExtent.width, windowsExtent.height, 1},
//         .arrayLayers = 1,
//         .mipLevels = 1,
//         .memoryUsage = MEMORY_USAGE_GPU_ONLY,
//         .type = RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_RENDER_TARGET,
//         .creationFlag = TEXTURE_CREATION_NONE
//     };
//     RHITextureRef depthTexture = backend->CreateTexture(depthTextureInfo);

//     // texture view
//     RHITextureViewInfo colorTextureViewInfo = {
//         .texture = colorTexture,
//         .format = colorTexture->GetInfo().format,
//         .viewType = VIEW_TYPE_2D,
//         .subresource = {TEXTURE_ASPECT_COLOR, 0, 1, 0, 1},
//     };
//     RHITextureViewRef colorTextureView = backend->CreateTextureView(colorTextureViewInfo);


//     RHITextureViewInfo depthTextureViewInfo = {
//         .texture = depthTexture,
//         .format = depthTexture->GetInfo().format,
//         .viewType = VIEW_TYPE_2D,
//         .subresource = {TEXTURE_ASPECT_DEPTH_STENCIL, 0, 1, 0, 1},
//         };
//     RHITextureViewRef depthTextureView = backend->CreateTextureView(depthTextureViewInfo);

//     // sampler
//     RHISamplerInfo samplerInfo = {
//         .minFilter = FILTER_TYPE_LINEAR,
//         .magFilter = FILTER_TYPE_LINEAR,
//         .mipmapMode = MIPMAP_MODE_LINEAR,
//         .addressModeU = ADDRESS_MODE_CLAMP_TO_EDGE,
//         .addressModeV = ADDRESS_MODE_CLAMP_TO_EDGE,
//         .addressModeW = ADDRESS_MODE_CLAMP_TO_EDGE,
//         .compareFunction = COMPARE_FUNCTION_NEVER,
//         .mipLodBias = 0.0f,
//         .maxAnisotropy = 0.0f, 
//     };
//     RHISamplerRef sampler = backend->CreateSampler(samplerInfo);

//     // shader
//     RHIShaderInfo vertShaderInfo = {
//         .entry = "main",
//         .frequency = SHADER_FREQUENCY_VERTEX,
//         .code = vertShaderCode
//     };
//     RHIShaderInfo fragShaderInfo = {
//         .entry = "main",
//         .frequency = SHADER_FREQUENCY_FRAGMENT,
//         .code = fragShaderCode
//     };
//     RHIShaderInfo computeShaderInfo = {
//         .entry = "main",
//         .frequency = SHADER_FREQUENCY_COMPUTE,
//         .code = compShaderCode
//     };
//     RHIShaderRef vertShader = backend->CreateShader(vertShaderInfo);
//     RHIShaderRef fragShader = backend->CreateShader(fragShaderInfo);
//     RHIShaderRef compShader = backend->CreateShader(computeShaderInfo);

//     // rootSignature
//     RHIRootSignatureInfo rootSignatureInfo = {};
//     rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
//                      //.AddEntryFromReflect(vertShader)
//                      //.AddEntryFromReflect(fragShader)
//                      .AddPushConstant({.size = 128, .frequency = SHADER_FREQUENCY_GRAPHICS})
//                      .AddEntry({1, 0, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
//                      .AddEntry({1, 1, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER});
//     RHIRootSignatureRef rootSignature = backend->CreateRootSignature(rootSignatureInfo);

//     RHIRootSignatureInfo computeRootSignatureInfo = {};
//     computeRootSignatureInfo.AddEntryFromReflect(compShader);
//     RHIRootSignatureRef computeRootSignature = backend->CreateRootSignature(computeRootSignatureInfo);

//     // descriptorSet
//     // RHIDescriptorSetRef descriptor = rootSignature->CreateDescriptorSet(0);
//     // RHIDescriptorUpdateInfo descriptorUpdateInfo = {
//     //     .binding = 0,
//     //     .index = 0,
//     //     .resourceType = RESOURCE_TYPE_RW_BUFFER,
//     //     .buffer = rwBuffer,
//     //     .bufferOffset = 0,
//     //     .bufferRange = rwBuffer->GetInfo().size
//     // };
//     // descriptor0->UpdateDescriptor(descriptorUpdateInfo);

//     // graphicsPipeline
//     RHIGraphicsPipelineInfo pipelineInfo = {};

//     pipelineInfo.vertexShader = vertShader;
//     pipelineInfo.fragmentShader = fragShader;

//     pipelineInfo.rootSignature = rootSignature;

//     pipelineInfo.primitiveType = PRIMITIVE_TYPE_TRIANGLE_LIST;
    
//     // pipelineInfo.vertexInputState.vertexElements.push_back({
//     //     .streamIndex = 0,
//     //     .attributeIndex = 0, 
//     //     .format = FORMAT_R32G32B32_SFLOAT,
//     //     .offset = 0,
//     //     .stride = 6 * sizeof(float),
//     //     .useInstanceIndex = false,
//     // });
//     // pipelineInfo.vertexInputState.vertexElements.push_back({
//     //     .streamIndex = 0,
//     //     .attributeIndex = 1, 
//     //     .format = FORMAT_R32G32B32_SFLOAT,
//     //     .offset = 3 * sizeof(float),
//     //     .stride = 6 * sizeof(float),
//     //     .useInstanceIndex = false,
//     // });

//     pipelineInfo.rasterizerState = {
//         .fillMode = FILL_MODE_SOLID,
//         .cullMode = CULL_MODE_NONE,
//         .depthClipMode = DEPTH_CLIP,
//         .depthBias = 0.0f,
//         .slopeScaleDepthBias = 0.0f
//     };

//     pipelineInfo.blendState.renderTargets[0] = {
//         .enable = false,
//         .colorBlendOp = BLEND_OP_ADD,
// 		.colorSrcBlend = BLEND_FACTOR_ONE,
// 		.colorDstBlend = BLEND_FACTOR_ZERO,
// 		.alphaBlendOp = BLEND_OP_ADD,
// 		.alphaSrcBlend = BLEND_FACTOR_ONE,
// 		.alphaDstBlend = BLEND_FACTOR_ZERO,
// 		.colorWriteMask = COLOR_MASK_RGBA
//     };

//     pipelineInfo.depthStencilState = {
//         .enableDepthTest = true,
//         .enableDepthWrite = true,
//         .depthTest = COMPARE_FUNCTION_LESS_EQUAL
//     };

//     pipelineInfo.colorAttachmentFormats[0] = colorFormat;
//     pipelineInfo.depthStencilAttachmentFormat = depthFormat;

//     RHIGraphicsPipelineRef graphicsPipeline = backend->CreateGraphicsPipeline(pipelineInfo);

//     // computePipeline
//     RHIComputePipelineInfo computePipelineInfo = {
//         .computeShader = compShader,
//         .rootSignature = computeRootSignature
//     };
//     RHIComputePipelineRef computePipeline = backend->CreateComputePipeline(computePipelineInfo);
    
//     // renderPass
//     RHIRenderPassInfo renderPassInfo = {
//         .extent = windowsExtent,
//         .layers = 1
//     };
//     renderPassInfo.colorAttachments[0] = {
//         .textureView = colorTextureView,
//         .loadOp = ATTACHMENT_LOAD_OP_CLEAR,
//         .storeOp = ATTACHMENT_STORE_OP_STORE,
//         .clearColor = {0.0f, 0.0f, 0.0f, 0.0f}
//     };
//     renderPassInfo.depthStencilAttachment = {
//         .textureView = depthTextureView,
//         .loadOp = ATTACHMENT_LOAD_OP_CLEAR,
//         .storeOp = ATTACHMENT_STORE_OP_STORE,
//         .clearDepth = 1.0f
//     };
//     RHIRenderPassRef renderPass = backend->CreateRenderPass(renderPassInfo);


//     // fence semaphore
//     std::vector<RHISemaphoreRef> startSemaphores;
//     std::vector<RHISemaphoreRef> finishSemaphores;
//     std::vector<RHIFenceRef> fences;
//     for(uint32_t i = 0; i < framesInFlight; i++)
//     {
//         startSemaphores.push_back(backend->CreateSemaphore());
//         finishSemaphores.push_back(backend->CreateSemaphore());
//         fences.push_back(backend->CreateFence(true));
//     }

//     bool initResource = false;
//     while(!glfwWindowShouldClose(window)) 
//     {
//         glfwPollEvents();

//         EngineContext::Tick(); 
//         uint32_t frameIndex = EngineContext::CurrentFrameIndex();

//         Move(transformComponent, EngineContext::GetDeltaTime(), window);    // 镜头移动



//         fences[frameIndex]->Wait();
//         RHITextureRef swapchainTexture = swapchain->GetNewFrame(nullptr, startSemaphores[frameIndex]);      

//         RHICommandListRef GraphicsCommand = commands[frameIndex];
//         GraphicsCommand->BeginCommand();
        
//         if(!initResource)
//         {
//             initResource = true;
//             // GraphicsCommand->BufferBarrier({indexBuffer, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_INDEX_BUFFER});
//             // GraphicsCommand->BufferBarrier({vertexBuffer, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_VERTEX_BUFFER});
//             // GraphicsCommand->BufferBarrier({rwBuffer, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_UNORDERED_ACCESS});
//             GraphicsCommand->TextureBarrier({colorTexture, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_COLOR_ATTACHMENT});
//             GraphicsCommand->TextureBarrier({depthTexture, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_DEPTH_STENCIL_ATTACHMENT});
//         }     

//         {
//             GraphicsCommand->PushEvent("RHI Render Pass", {0.0f, 0.0f, 0.0f});

//             GraphicsCommand->BeginRenderPass(renderPass);
//             GraphicsCommand->SetViewport({0, 0}, windowsOffset);
//             GraphicsCommand->SetScissor({0, 0}, windowsOffset); 
//             GraphicsCommand->SetGraphicsPipeline(graphicsPipeline);
//             GraphicsCommand->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
//             // GraphicsCommand->BindDescriptorSet(descriptor1, 1);
//             // GraphicsCommand->PushConstants(&pushConstants, 128, SHADER_FREQUENCY_GRAPHICS);
//             for(uint32_t i = 0; i < model->GetSubmeshCount(); i++)
//             {
//                 VertexBufferRef vertexBuffer = model->GetVertexBuffer(i);
//                 IndexBufferRef indexBuffer = model->GetIndexBuffer(i);
//                 // GraphicsCommand->BindVertexBuffer(vertexBuffer->buffer, 0, 0);
//                 // GraphicsCommand->BindIndexBuffer(indexBuffer->buffer, 0);
//                 // GraphicsCommand->DrawIndexed(indexBuffer->IndexNum(), 1, 0, 0, i + 1); 

//                 GraphicsCommand->Draw(indexBuffer->IndexNum(), 1, 0, i + 1);
//             }  
//             GraphicsCommand->EndRenderPass();

//             GraphicsCommand->TextureBarrier({colorTexture, RESOURCE_STATE_COLOR_ATTACHMENT, RESOURCE_STATE_TRANSFER_SRC});
//             GraphicsCommand->TextureBarrier({swapchainTexture, RESOURCE_STATE_PRESENT, RESOURCE_STATE_TRANSFER_DST});
//             GraphicsCommand->CopyTexture(   colorTexture, {TEXTURE_ASPECT_COLOR, 0, 0, 1}, 
//                                     swapchainTexture, {TEXTURE_ASPECT_COLOR, 0, 0, 1});
//             GraphicsCommand->TextureBarrier({colorTexture, RESOURCE_STATE_TRANSFER_SRC, RESOURCE_STATE_COLOR_ATTACHMENT});
//             GraphicsCommand->TextureBarrier({swapchainTexture, RESOURCE_STATE_TRANSFER_DST, RESOURCE_STATE_PRESENT});

//             GraphicsCommand->PopEvent();
//         }


//         GraphicsCommand->EndCommand();
//         GraphicsCommand->Execute(fences[frameIndex], startSemaphores[frameIndex], finishSemaphores[frameIndex]);

//         swapchain->Present(finishSemaphores[frameIndex]);       
//     }


// }
//
//struct Vec3 { float x, y, z; };
//struct Ray { Vec3 o; Vec3 d; };          // 注意：d 未归一化
//struct AABB { Vec3 min, max; };           // 轴对齐包围盒
//
//// 射线与盒子相交则返回 true，并把「最近的进入距离」写入 t_out
////（t 以 o 为起点，沿 d 方向；t_out 应 >= 0）
//bool intersectRayAABB(const Ray& r, const AABB& b, float& t_out)
//{
//	float t_min = -INFINITY, t_max = INFINITY;
//    const float o[3] = { r.o.x, r.o.y, r.o.z };
//    const float d[3] = { r.d.x, r.d.y, r.d.z };
//	const float lo[3] = { b.min.x, b.min.y, b.min.z };
//	const float hi[3] = { b.max.x, b.max.y, b.max.z };
//
//    for (int i = 0; i < 3; i++)
//    {
//        if (d[i] == 0)
//        {
//			if (o[i] < lo[i] || o[i] > hi[i]) return false; // 平行且不在盒子范围内
//        }
//        else
//        {
//			float speed = 1.f / d[i];
//			float t1 = (lo[i] - o[i]) * speed;
//			float t2 = (hi[i] - o[i]) * speed;
//			if (t1 > t2) std::swap(t1, t2);
//			t_min = std::max(t_min, t1);
//			t_max = std::min(t_max, t2);
//            if(t_min > t_max) return false;
//        }
//    }
//
//    if (t_max < 0.f)return false;
//    t_out = t_min > 0.f ? t_min : 0.f;
//	return true;
//
//}



//class LRUCache {
//public:
//    LRUCache(int capacity);          // capacity >= 1
//    int  get(int key);               // 命中返回 value，否则返回 -1；命中算一次「最近使用」
//    void put(int key, int value);    // 键已存在则更新值；新键且已满则淘汰「最久未使用」的那个
//
//private:
//    struct Cache {
//        Cache* pre = nullptr;
//        Cache* post = nullptr;
//        // 用于反向查询
//        int key = 0;
//        int value = 0;
//    };
//    int capacity;
//    std::unordered_map<int, Cache> datas;
//    Cache* listhead = nullptr;
//    Cache* listend = nullptr;
//};
//
//LRUCache::LRUCache(int capacity)
//    :capacity(capacity)
//{
//    datas.reserve(capacity);
//    listhead = nullptr;
//    listend = nullptr;
//}
//
//int LRUCache::get(int key)
//{
//    auto it = datas.find(key);
//    if (it == datas.end())
//        return -1;
//    else
//    {
//        auto& Cache = it->second;
//
//        if (&Cache != listhead)
//        {
//            // 如果这个cache是最后一个，则更新listend
//            if (&Cache == listend)
//                listend = Cache.pre;
//            // 从链表的原位移出
//            if (Cache.pre != nullptr)
//                Cache.pre->post = Cache.post;
//            if (Cache.post != nullptr)
//                Cache.post->pre = Cache.pre;
//
//            // 加入链表头部
//            listhead->pre = &Cache;
//            Cache.post = listhead;
//            Cache.pre = nullptr;
//            listhead = &Cache;
//        }
//
//        // 返回值
//        return it->second.value;
//    }
//}
//
//void LRUCache::put(int key, int value)
//{
//    auto it = datas.find(key);
//    if (it != datas.end())
//    {
//        auto& Cache = it->second;
//        Cache.value = value;
//
//        if (&Cache != listhead)
//        {
//            // 如果这个cache是最后一个，则更新listend
//            if (&Cache == listend)
//                listend = Cache.pre;
//
//            // 从链表的原位移出
//            if (Cache.pre != nullptr)
//                Cache.pre->post = Cache.post;
//            if (Cache.post != nullptr)
//                Cache.post->pre = Cache.pre;
//
//            // 加入链表头部
//            listhead->pre = &Cache;
//            Cache.post = listhead;
//            Cache.pre = nullptr;
//            listhead = &Cache;
//        }
//    }
//    else
//    {
//		// 淘汰最久未使用的
//        if (datas.size() == capacity)
//        {
//            int erase_key = listend->key;
//            listend = listend->pre;
//            datas.erase(erase_key);
//        }
//
//        // 新数据加入
//        Cache cache;
//        cache.value = value;
//        cache.key = key;
//        datas.insert(std::make_pair(key, cache));
//
//        datas[key].post = listhead;
//        listhead = &datas[key];
//        // 首个加入的数据
//        if (datas.size() == 1)
//            listend = &datas[key];
//    }
//}

//class LRUCache {
//public:
//    LRUCache(int capacity);          // capacity >= 1
//    int  get(int key);               // 命中返回 value，否则返回 -1；命中算一次「最近使用」
//    void put(int key, int value);    // 键已存在则更新值；新键且已满则淘汰「最久未使用」的那个
//
//private:
//    int capacity;
//	std::list<std::pair<int, int>> cacheList; // 双向链表，存储键值对
//	std::unordered_map<int, std::list<std::pair<int, int>>::iterator> cacheMap; // 哈希表，存储键到链表节点的映射
//};
//
//LRUCache::LRUCache(int capacity) : capacity(capacity) {cacheList.resize(capacity);cacheMap.reserve(capacity); }
//
//int LRUCache::get(int key)
//{
//	auto it = cacheMap.find(key);
//    if (it != cacheMap.end()) return -1;
//	cacheList.splice(cacheList.begin(), cacheList, it->second);
//	return it->second->second;
//}
//
//void LRUCache::put(int key, int value)
//{
//	auto it = cacheMap.find(key);
//	if (it != cacheMap.end())
//	{
//		it->second->second = value;
//		cacheList.splice(cacheList.begin(), cacheList, it->second);
//	}
//	else
//	{
//		if ((int)cacheList.size() == capacity)
//		{
//			cacheMap.erase(cacheList.back().first);
//			cacheList.pop_back();
//		}
//		cacheList.emplace_front(key, value);
//		cacheMap[key] = cacheList.begin();
//	}
//}


//// 假设有无限个并行执行单元，返回完成所有任务的最短总时间
//int criticalPath(int N, const std::vector<int>& cost, const std::vector<std::pair<int, int>>& edges)
//{
//
//    // 我的想法是将这个图分解成一个个独立的层，每一层上的任务可以并行，理论上最多n层
//    int res = 0;
//
//
//    // 第i个任务  {依赖的第i个任务的任务 }
//    std::unordered_map<int, std::vector<int>> layerMap;
//    // 第i个任务的直接依赖任务数量,以及以当前这个节点作为结束的最短总时间
//	// 问题2的回答第二点，当要求这个 最早开始时间 / 最晚开始时间时，我们要改成 std::unordered_map<int, std::pair<int,std::pair<int,int>>>DepMap; 其中第一个int是依赖数量，第二个pair的第一个int是最早开始时间，第二个int是最晚开始时间
//    std::unordered_map<int, std::pair<int, int>>DepMap;
//    DepMap.reserve(N);
//    layerMap.reserve(N);
//
//	// 等待访问的任务，这个任务的前置任务都已经完成了
//    // 默认所有任务都可以
//    auto range = std::ranges::iota_view(0, N);
//    std::unordered_set<int> visited(range.begin(), range.end());
//    
//    // 这里的遍历方式错了
//    for (int i = 0; i < N; i++)
//    {
//        auto& [form, to] = edges[i];
//        layerMap[form].push_back(to);
//
//        DepMap[to].first++; // 增加依赖数量
//        // TO这个任务要移除
//		visited.erase(to);
//    }
//
//	// 少了一步将visited的代价加入到DepMap中（也就是哪些开始就没有依赖的任务的代价，不然在下面他们的代价就变成0了）
//
//	// 肯定至少有一个任务没有依赖，作为第一层
//    while (!visited.empty())
//    {
//        // 本轮访问的任务
//		int it = *visited.begin();
//        visited.erase(it);
//
//        // 就算是加入的新值也是0，没有影响
//        int it_cost = DepMap[it].second;
//
//        res = std::max(res, it_cost);
//
//        // 遍历依赖当前访问任务的任务
//        for (auto& to : layerMap[it])
//        {
//            // 更新以当前访问任务为前置任务的任务的最短总时间
//            // 问题2的回答
//            // 在这里我们要同时更新最早开始时间和最晚开始时间
//            DepMap[to].second = std::max(DepMap[to].second, it_cost + cost[to]);
//
//			// 依赖数量减一
//			DepMap[to].first--;
//			// 如果依赖数量为0，说明这个任务可以执行了
//			if (DepMap[to].first == 0)
//			{
//				visited.insert(to);
//			}
//        }
//    }
//
//    return res;
//}
//// 问题1的回答
//// 算法的复杂度为O(V+E),算法实际上就是完整遍历了两遍所有的边。本质是求DAG的关键路径
//
//// 问题2的回答
//// 对于DAG而言，至少会存在一个起点和一个终点，二从起点到终点的所有路径中这个关键路径是耗时最长的一个，所以无限并行的情况下，总时间就等于这条关键路径的长度
//// 松弛时间（slack）不知道是什么意思，要倒着求
//
//// 问题3的回答
//// 工程近似 = 列表调度（list scheduling）：给每个任务定一个优先级 rank，每步把「就绪且 rank 最高」的任务贪心派到最早空闲的核上



// 第一问的回答
// bug在于当一个线程经过两次检测开始构造示例的时候（还没构造完成），另一个线程再次调用这个函数会得到实例不为空的判定直接返回实例（但是也没有构造完成），也就是返回一个
// 构造了一半的实例，出现了问题

// 第二问的回答
// 我们将第一次检测去掉就可以了，当一个线程在构造实例的时候，与其让其他线程拿到一个空的结果不如让他直接等待构造完成再返回实例，而同时并发访问get的时候这个函数
// 又没有什么复杂的操作，所以不会出现性能问题
// 提示的这种改发我不太会，你教我一下

// part2这一整个我都不太会，volatile不是禁止编译器优化变量的吗？为什么会用在这？
// 
// - 缓存以 64 字节（一个缓存行） 为单位读写。a、b 两个 int 紧挨着（相隔 8 字节），必然在同一缓存行里。
// - 多核一致性用 MESI 协议：线程 1（核 1）写 a，这一行在核 1 变成 Modified，核 2 手里那份被 Invalidate。线程 2 下次写
// b，发现自己的行失效了，得先去核 1 那儿把整行拉过来（一次一致性 miss），写完又把核 1 的失效掉。
// - 于是两个「逻辑上毫无关系」的变量，被缓存行绑在了一起，每次写都把对方核的行踢失效，像打乒乓球一样来回抢同一行。每次这种一致性 miss
// 要几百个周期，而命中只要几个周期——所以慢一大截。这就叫伪共享：没有逻辑共享，却有物理共享（同一缓存行）


// 第一问，vector的大小(size)是N的时候，我们遍历的下标应该是0到N-1，而不是0到N，这里会出现数组访问越界
// 修复：将i <= v.size()改成i < v.size()

// 第二问，int除以int得到的结果是int，会有精度丢失，应该转成double后在做除法
// 修复： return sum / count;改成 return sum / (double)count;

// 第三问，比较的方式为v[i] > best，当数组中的元素全部都是小于0的时候，会一直没有出现有效的对比，导致返回v[0]
// 修复：将best初始化为v[0]，或者best = INTMIN

// 第四问，函数内部构造的是临时变量，一但函数结束，这个变量就会被销毁，导致返回的指针指向无效内存
// 修复，要么将变量加上static，要么将返回值改成拷贝返回

// 第五问，对于持有一个原始指针类型的类，如果没有实现对应的深度拷贝函数，会导致出现两个类变量里面的指针指向同一份内存，从而出现双倍释放等问题
// 修复：添加对应的深度拷贝函数

// 第六问，如果传入的是空vector会导致return v[idx]时出现越界访问，这个问题修复应该需要一个明确的约束结果，表述无这个情况

// 第七问，如果数很多且很大可能出现int溢出的情况，改成long long 应该就可以了，如果还是不行就要改成自己实现高位加法了

//// 「写自己的索引用release，读对方的索引用 acquire，索引单调递增用无符号差判满空，两个索引分缓存行」
//template<typename T,size_t Capacity>
//class SPSCQueue
//{
//    static_assert(Capacity& (Capacity - 1) == 0, "Capacity 必须是 2 的幂");
//public:
//
//    // 生产者
//    bool try_push(const T& item)
//    {
//        size_t tail = tail_.load(std::memory_order_relaxed);
//        size_t head = head_.load(std::memory_order_acquire);
//
//        if (tail - head == Capacity) return false; // 已满
//
//        buf_[tail % (Capacity - 1)] = item;
//        tail_.store(tail + 1, std::memory_order_release);
//        return true;
//    }
//
//    // 消费者
//    bool try_pop(T& item)
//    {
//        size_t tail = tail_.load(std::memory_order_acquire);
//        size_t head = head_.load(std::memory_order_relaxed);
//
//        if (tail == head)return false;  // 已空
//
//        item = buf_[head % (Capacity - 1)];
//        head_.store(head + 1, std::memory_order_release);
//        return true;
//    }
//
//private:
//
//    alignas(64) std::atomic<size_t> head_{ 0 };
//    alignas(64) std::atomic<size_t> tail_{ 0 };
//    std::array<T, Capacity> buf_;
//};


// 第一问回答
// head是前面那个属于生产者，每push一个加一，tile是消费者紧跟在head后面，每pop一个加一。整体采用的是取模的方式

// 第二问回答
// 我不太清楚每一个内存序是什么意思，试着大概写了一下，将用memory_order_acquire做数据的初步查询，满足条件后加入强制内存序memory_order_relaxed，最后将修改的
// 结果同步到每一个线程memory_order_release

// 第三问的回答
// 由于回绕只会产生一次，所以我额外添加了一个是否产生了回绕的bool值。当产生回绕时，已经存在的容量不可以简单的使用head-tile来计算，正确的结果
// 应该是-(head-tile)%Capacity

// 第四问的回答
// head和tile本质上是没有什么关系的，pop修改的只有tile，push修改的也只有head，对于另一个只是简单的查询一下，
// 所以如果我们将其写在一个缓存行里面就会出现消费线程1pop时只要写tile结果将生产线程2手中的缓存行设置成无效的了，即便他没有写线程2手中的head



// 第一问的回答
// 虚函数表和虚指针我不太清楚是个什么东西？但是我知道虚指针是在类的实例内存里面的头8字节里面，应该是类的实例自己持有吧？

// - vptr（虚指针）：存在每个多态对象的内存里（通常对象头，64 位下 8 字节）
// - vtable（虚函数表）：每个类一份，放在只读数据段（.rodata），每个条目是该类某个虚函数的函数指针，按声明顺序排。
//
// 对象内存(每对象一份)          vtable(每类一份, rodata)
//┌───────┐           ┌──────────┐
//│ vptr ────┼──── > │[0] & Derived::f    │
//├───────┤           │[1] & Derived::g    │
//│ 成员数据...  │           │[2] ...             │
//└───────┘           └──────────┘


// 第二问的回答
// 读对象头 vptr        → 拿到 vtable 地址
// 按偏移 k 读 vtable    → 拿到函数指针 &Derived::f
// 间接跳转到该指针       → 执行

// 第三问的回答
// 子类如果没有重写父类的析构函数，那么当子类的实例析构的时候是调用的父类的析构函数（通过基类指针 delete），当子类有自己特有的数据时就会出现问题，比如指针的内存没有没有释放等

// 第四问的回答
// vptr 在构造链里被逐步重设

// 第五问的回答
// - 两重间接：对象→vptr→vtable→函数指针（两次解引用）。
// - 阻断内联（最贵）：编译器编译期不知道 p->f()跳哪，没法把函数体内联进调用点。小函数每帧百万次，不能内联的代价往往比两次跳转还大。
// - 缓存：vtable 可能冷（cache miss），间接跳转还打击分支预测（同一 p->f() 可能跳不同目标）。

// 第六问的回答
// draw()函数是虚函数，当每帧几十万个对象调用这个draw()函数时都会触发间接寻址来调用真正的draw()子类函数，这里的间接寻址和多个draw()子类实现函数之间
// 跳转都会有非常大的性能损耗

// 第七问的回答
// 关于你写的4个解决的名称我都有点看不懂，我知道的是根据这个（间接寻址和多个draw()子类实现函数之间跳转），关于第一点是将这个间接寻址给消除掉
// 比如将draw()子类函数都变成一个直接的函数，不再采用重写的虚函数的方式调用。关于第二点，我们可以在整体遍历调用draw()子类前，将这几十万个对象按照每一个具体的
// draw()子类进行分流，然后就可以避免一直出现跳转。



// 第一问的回答
// 关于分配策略，第一步肯定是直接new一个足够大的完整内存，然后再将内存依次按需分配和回收，这样就不会出现频繁调用操作系统函数和内存碎片了
// 这里的依次按需分配和回收整体可以有两个思路，其一是不管回收（即只要一个指向“足够大的完整内存”的起始指针ptr和一个已经分配出去的内存大小(单增)size）,当有分配需求
// 时，只需要将需求大小对齐计算一下得到need，然后将ptr+size返回回去，size+need即可。
// 第二种则是将“足够大的完整内存”按照对齐分块，比如将256分成128和2x64三块，每块包含size和他们在起始指针的offset，然后按照大小分别用多个链表串起来，分配时
// 将需求大小对齐计算一下得到need，取一个和need相同大小的块即可，如果对应链表上的空闲块没有了，那就从上层链表中取出一个块切分放到下层链表中。回收也只需要
// 将分配出去的offset和size返回来重新构成一个空闲块加入链表

// 第二问的回答
// 如果没有做约束，这个情况是会发生的，假设有A/B两帧并发，当CA1做完以后，GA1开始，CB1完成后，GB1开始，当再次开始A时，如果此时的CA2没有和GA1存在约束（fence），
// 就会出现GPU还在读取数据（GA1），但是CA2就开始改数据了，解决方式很简单，A/B各持有一个信号量即可，CPU每帧等待这个信号量(为true通过)，然后GPU开始的时候置为false
// GPU完成的时候置为true，这样在上面将的CA2和GA1中间就有一个约束了，只有GA1完成将这个信号量置为true时CA2才可以开始工作

// 第三问的回答
// 我们一般是将内存对齐为2的幂，所以我们可以取到所需size的二进制表述中第一个为1的位，再向前推1位的2的幂次方，应该就是我们需要的对齐大小
// p=0x1003, a=16 → (0x1003+0xF) & ~0xF = 0x1012 & 0xFFF0 = 0x1010
//uintptr_t align_up(uintptr_t p, uintptr_t a) {
//    return (p + a - 1) & ~(a - 1);   // a=16: (p+15) & ~15
//}

// 第四问的回答
// 大对象肯定是选择新加一个链表，然后每一次new，添加到这个链表上，帧尾再统一遍历这个链表上的每一块delete回收