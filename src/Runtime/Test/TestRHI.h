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



// 第一问的回答
// 从整体GPU帧耗时方面上看，完美理想状态的耗时与实际帧耗时存在 2.1ms+13.8ms-12.5ms = 3.4ms的缺口，明显远大于可能因为pass移动而带来的跨队列信号量的时间消耗，
// 所以肯定是没有完全并行成功，所以我们应该是利用vulkan提供的query功能在时间线链条上做到pass的打点（开始到结束），然后再在提交Semaphore的部分做日志分析，
// 将W/S定位到具体pass上，利用这个时间线，这个额外的时间空窗就可以很容易定位出来

// 第二问的回答
// 有了这个时间空窗以后，我们就分析问题原因所在
// 1.这个compute pass确实是和他在Graphics队列上相邻的pass没有依赖关系，可以存在并行收益，但是异步计算的队列是满负荷运转，导致整体上没有任何时间上的收益还倒贴同步信号量损耗
// 2.1.的前置情况下，且异步计算有空缺，但是调度问题将他的运行安排在了时间线原来点的后方，导致触发了Graphics队列后面pass与他的依赖关系，没有吃到Graphics队列上相邻的pass没有依赖关系的并行红利
// 3.这个compute pass确实是和他在Graphics队列上相邻的pass本身就存在依赖关系，没有任何并行可言，只将他移动到异步计算队列没有任何意义
// 4.1.的前置情况下，且异步计算有空缺，但是将他实现异步后导致与他异步的那部分pass的执行时间大幅延长，吃完了并行收益


// 第三问的回答
// 我们应当先验证1.的前置条件是否成立，
// 对于该pass的前面pass，是否存在资源R-A-W，W-A-W的情况或者资源是独享的且Graphics队列与异步计算队列不同族则额外有R-A-R，没有代表存在与前方pass并行可能，
// 对于该pass的后面pass，是否存在资源W-A-W，W-A-R的情况或者资源是独享的且Graphics队列与异步计算队列不同族则额外有R-A-R，没有代表存在与后方pass并行可能，
// 若都不行，则坐实原因3。否则我们更进一步，若完整帧时间 == 异步计算队列帧时间且异步计算队列没有空转，则坐实原因1。
// 再进一步就有两个方向可以并行验证，一个是看时间线，Graphics队列上的该pass的后续pass中有一个pass在等待这个pass，且出现了空窗大约1ms，坐实原因2
// 二个是看原该pass点周边的pass，也就是我们期待并行与之的pass的耗时出现了明显的增长，吃掉了并行收益，坐实原因4


// SM/CU竞争              多队列并行在同一套SM上，竞争warp槽和调度带宽       SM busy% / achieved occupancy  / 各pipe利用率

// L2缓存污染             将另一个pass的数据挤出L2，跑更多的DRAM             L2 hit rate / L2 sector 流量
    
// DRAM带宽饱和           双双内存子系统排队                                 DRAM 带宽% > 90 且 memory latency上升

// 功耗/频率墙            并发拉高瞬时功耗，DVFS降频                         core clock曲线  power / temp


// 第一问回答
// 这是一个延迟受限的瓶颈，
// Memory Throughput (L1/LSU侧) 和 L2 Hit Rate展示缓存部分的数据，虽然偏低，但是远不止于将pass的运行时间延长到两倍以上，也不是容积的问题
// DRAM Throughput，这是DRAM带宽的数据，明显没有达到瓶颈，也不是吞吐方面的问题
// Warp Stall 分布（Top3）则是warp槽的工作状态，62标志每个warp槽都在工作中，没有存在其他的打断，说明是纯粹工作速度变慢了
// Compute (SM) Throughput和Achieved Occupancy严重偏低，其中SM的指令发出占比只有1/4，也就是说每个sm大约只有1/4的warp被启用了。问题就在这里

// 第二问的回答
// 寄存器限制 = （65536）/ （128 * 128） = 4 ，也就是4/64 = 6.2%
// 内存限制 = 96KB/48KB = 2，也就是2/64 = 3.1%
// ？应该是内存限制

// 第三问的回答
// SM中的warp槽占比过低是寄存器和内存共同限制的，现在是内存限制更大，跳转寄存器没有用

// 第四问的回答
// 将每一个线程的寄存器数降下来，提高SM利用率
// 将每一个工作组的内存占比降下来，提高SM利用率
// 优化pass调度，将使用相近资源的pass放在一起，提高缓存命中率，这个指标也有点低

// 第五问的回答
// 可以使用tile 的方式，对同一画面做连续pass处理，不用DRAM做数据读取，直接保留使用

//  CA  |   CB    |     CA
//      |   GA    |     GB

// 第一问的回答
// 当飞两帧的时候，我们可以把整个过程分成A/B两个部分，对于当下的情况第一帧是运行的CA，第二帧是：CB与GA并行，第三帧是CA与GB并行。但是我们添加了一个小 buffer之后
// A与B之间出现了依赖关系，对于之前的第二帧而言，CB需要GA运行后的数据（各个pass耗时）才可以运行，结果流水线深度从2变成1也就是第一帧CA，第二帧GA和CB串行，
// 第三帧GB与CA串行

// 第二问的回答
// 由1可知，在原版中 1000ms/max(C,G) = 62.5fps,新版中 1000ms/(C+G) = 33.3fps,与观测的一致。由1可知，每一次G之前都要完整等待一个C，也就是16ms，和观测的一致

// 第三问的回答
// 问题的本质在于飞行的A/B帧出现了依赖，即CB依赖GA，所以我们要将CB重新依赖回GB，也就要多延迟一帧，由此我们也就可以轻易得到对于MAX_FRAMES_IN_FLIGHT的情况下，读回延迟K帧的
// K的下界就在MAX_FRAMES_IN_FLIGHT，本质在于让MAX_FRAMES_IN_FLIGHT每一帧槽都没有彼此间的依赖，
// 环形缓冲就创建MAX_FRAMES_IN_FLIGHT份数据，按 frameIndex % MAX_FRAMES_IN_FLIGHT 轮转即可

// 第四问的回答
// 缓冲垫

// GPU饿死：在多出的时间里面没有提交
// Present队列排空：交换链里排队的图像被耗尽
// 缓冲垫要逐帧回填

// 上传占用PCIe带宽，分配器碎片化，新资源需要Pso编译


// 第一问的回答
// jitter : 每帧投影亚像素偏移，采样同一像素不同子位置（N帧循环）
// 累积: 循环一轮后约等于 时间上 Nx 超采样，达到抗锯齿的目的（人话说就是将单帧Nx超采样改成N帧单采样+历史帧混合）

// 鬼影：1.重投影不准 2.重投影位置历史帧无有效数据

// 第二问的回答
// 静态几何重投影点：本帧depth + 本帧逆投影矩阵重建世界坐标 + prev ViewProj 投回去
// 动态几何重投影点：顶点数据需要 current + prev model matrix 输出 currUV - prevUV
// 蒙皮陷阱：蒙皮会改变顶点位置，除了prev model matrix，还要prev bone palette
// 边缘像素的velocity不知道是物体的前景还是背景，需要 dilation（借邻域速度）
// 轮廓边缘上的像素，几何上同时被两个表面覆盖

// 第三问的回答
// Neighborhood clamping:本帧3x3盒子来clamping history color
// history有效性检测(同物体，距离，法线，坐标)
// 速度调制 blend，快少信历史
// 上面的边缘像素问题
// 累积域：要转到YCoCg的tonemap之后在做累积，直接在HDR域做亮像素会主导min/max

// 第四问的回答
// clamping太紧       夹成当前帧，锯齿回归
// blend抬太猛        NRD 的随机信号没人平均
// 拒绝太激进         高光/粒子闪烁


// 第一问的回答
// Clip = P * V * W
// 两个深度比较
// shadow Map 中记载的texel深度：每个级联一个，远处级联一个texel可能是多个世界点的最小深度
// 相机中像素的深度：相机空间(NDC)uv通过depth重建世界点，再投影到级联的texel上比较
// float往返插值残余          接收侧 0.0002
// 坡度 × texel 量化误差     写入侧 bias = constantFactor + slopeFactor × (∂z/∂x 或 ∂z/∂y，取大者)

// 第二问的回答
// 我们将摄像机视锥按照.z切分成N层，每层单独计算shadow map，问题都出现在light.vp这一个关键点上
// 1.为了每层只计算本层所有的物体深度，light.vp需要依赖于摄像机的视锥
// 2.light.v的forward/up 逐帧变
// 3.frustumCorners和radius

// 关键在于将frustumCorners移动按照texelworld为最小单位在.xy分量上移动(光view空间，仅R，T会调整)，texelworld = 2 * radius / DIRECTIONAL_SHADOW_SIZE，最后将
// radius对齐一下texelworld


// 0 1 2 10 11 12 19 20 21
// 4 5 6 13 14 15 22 23 24
// 7 8 9 16 17 18 25 26 27
// 28 29 30 37 38 39 46 47 48
// 31 32 33 40 41 42 49 50 51
// 34 35 36 43 44 45 52 53 54
// 55 56 57 64 65 66 73 74 75
// 58 59 60 67 68 69 76 77 78
// 61 62 63 70 71 72 79 80 81

// e.g. 比如我们将shadow map的每一个texel块记载为S_M_1、S_M_2 .....S_M_n(目前我们 n = DIRECTIONAL_SHADOW_SIZE * DIRECTIONAL_SHADOW_SIZE个)，然后假设每一个 S_M_1 块记载
// 的深度都是由9个W_P_1、W_P_2...W_P_9的深度值对比得到的，也就是 S_M_1 = min(W_P_1、W_P_2...W_P_9),现在我们假设有个3x3的S_M块，其中每一块都是由9个W_P点组成，
// 并且每一个W_P点的深度就是他的数字编号，比如S_M_1 = min(W_P_1、W_P_2...W_P_9) = 1。S_M_3 = min(W_P_19、W_P_20...W_P_27) = 19,
// S_M_9 = min(W_P_73、W_P_74...W_P_81) = 73,后面将W_P_n简写为n
// 由此可知当我们在进行方向光计算时候，对于点41来说他采样的texel块为S_M_5 = 37, 37 < 41，所以这一个点在阴影里面，但是当我们向右下方偏移一个W_P单位时，41还是
// 采样到S_M_5,但是此时S_M_5的值就变成了41，又变成不在阴影里面了，这就是阴影游动的由来。所以我们将偏移的单位改成texel的大小，这样当发生偏移的时候，变的也是只是
// S_M的编号，每一个W_P所参与比较的W_P是不变的，简而言之就是W_P与他周边的W_P绑定在一起(41与37到45绑定在一起)，偏移前他们共同构成S_M_5,片以后他们共同构成S_M_1,S_M
// 只是编号变了，值没有变化，比较的结果也就不会发生变化，阴影就固定下来了


// 第三问的回答
// 对于给定的变换NDC空间 Δnx * Δny * Δnz,经过逆变换到view空间后的为 (N·F / (F − Δnz·(F−N))) * ( A * T * Δnx, - T * Δny, 1 ),其中A，T，F，N均为常量
// 且函数A * T * (N * F / (F − Δnz * (F−N)))的导数大于0，所以随着深度的增加，屏幕空间上的单一像素对应的世界空间上的跨度也会增加，而我们每一个shadow map中的texel对应的
// 恒定为 2 * radius / DIRECTIONAL_SHADOW_SIZE，要求对于每一个shadow map的负责区域，他的texel对应的世界空间上的跨度应该小于负责区域中的所有屏幕空间上的单一像素对应的世界空间上的跨度
// x，y轴上分别有A*T和T的一致放大比例，所以考虑一个就行
// 像素和 texel 都是在横向（屏幕 xy / 光源 xy）上离散采样的

// S的值是在nz从0到1之间打的点，即级联的切分比例
// e.g. 0 < nz < S(1) = 0.25 ，级联第0层距离S(0) = 0.0
// 
//              (A² + 1) * T² * N²F²       ⎡ N * F * S * (F−N) ⎤²    
// d(S) = sqrt( ────────── + ──────────────── )
//                 (F−S(F−N))²        ⎣2(F−S(F−N))(F−(S(F−N)) / 2)⎦ 

// 就是 F(nz)(1) = 2 * d(S(1)) / DIRECTIONAL_SHADOW_SIZE   应当要和 F(nz)(2) = (2/H) * T * N * F / (F − nz·(F−N)) 比较，F(nz)(1)小于F(nz)(2)代表精度高，等于代表精度正好，
// 大于代表精度低,联立取等号就可以试着推断S的最佳划分了

// CSM级联间的缝本质是在不连续的图中(级联的每一个shadow map)中做连续采样
// 空间上多采样或者时间上分次采样



// 第四问的回答


// 太阳圆心S(0,0),半径Rs，A(0,Rs),B(0,-Rs)
// 遮挡物圆心M(D,0),半径Rm,C(0,Rm),D(0,-Rm)
// 遮挡物到阴影的垂直距离为s
// 太阳为点 -> Rm / D = r0 / (D+s)
// 太阳为面 -> 
//              y本影上 = r0 - Rs * S / D
//              y半影外 = r0 + Rs * S / D

// PCSS：(目前仅在有大小的光源上实现)将NDC空间中对比出的depth差逆投影回世界空间，然后乘以太阳斜率后再重投影到对应纹理的UV空间上做PCF的核宽（参数不好调）
// 
// VSM：将PCF的多轮对比一次平均改成存depth(m₁ = depth、m₂ = depth * depth)，利用概率变成多轮平均+一次对比。利用上界p = σ²/(σ² + (x−μ)²) 做直接当作可见度,
// 第二个矩用于求σ。
// 注意x需要大于μ，否则为1
// 设 X = 滤波足迹内的深度分布,已知两阶矩μ、σ²，接收者在深度 x，可见度 V = P(X ≥ x)。
//  t > μ（半影/阴影侧）：遮挡物比接收者更靠近光源，计算可见度上界
//  t < μ（全亮侧）：足迹均值在接收者身后，这是"被挡比例"的上界，可见度上界只有1
// 
// 
// ESM：
//      V₊ = E[exp(c * (d − d_r))]  亮光侧
//      S₋ = E[exp(c * (d_r − d))]  暗光侧
// 要是可以在预滤波的时候做钳制是最好的，但是没有d_r
// 
// EVSM:
// 1.学习ESM正负两侧各出一套矩，分别计算可见度上界然后求min
// 2.利用换元 x = exp(c * x) ,让矩阵被单峰垄断，exp(+c * d)让远峰垄断矩，避免背部漏光，exp(−c * d)让近峰垄断矩，修镜像配置(接收者夹在近遮挡和远背景之间)
// 
// MSM:改成存4个矩，从VSM的单峰轮廓变成双峰轮廓(半影的标准几何)
// 

// 附加问题的回答
// 1像素的光追再面光的情况下，噪音是一个伯努利实验，方差的最高值正好在半影的正中心，导致降噪很难做