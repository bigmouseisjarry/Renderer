#include "VulkanRHI.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/VulkanRHI/VulkanUtil.h"
#include <SDL3/SDL.h>
#include "VulkanRHIResource.h"
#include "Function/Render/RHI/RHIResource.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RHI/RHI.h"
#include "Platform/HAL/PlatformProcess.h"
#include "Core/Log/Log.h"
#include "implot.h"

#include <cassert>
#include <cstddef>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include "ImGuizmo.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

VulkanRHIBackend::VulkanRHIBackend(const RHIBackendInfo& info) 
: RHIBackend(info)
, sync(PlatformProcess::CreateMutex())
{
    CreateInstance();   
    CreatePhysicalDevice();
    CreateLogicalDevice();
    CreateQueues();
    CreateMemoryAllocator();
    CreateDescriptorPool();
    CreateImmediateCommand();
}

void VulkanRHIBackend::Tick()
{
    RHIBackend::Tick();
    //immediateCommand->Flush();  // 每帧更新immediate
}

void VulkanRHIBackend::Destroy()
{
    for(auto& list : queues)
    {
        for (auto& queue : list) queue->WaitIdle();
    } 

    if(initImGui) 
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        // ImGui_ImplGlfw_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
    
    RHIBackend::Destroy();

    //renderPassPool.Clear();
    //frameBufferPool.Clear();
    vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);

    vmaDestroyAllocator(memoryAllocator);
    vkDestroyDevice(logicalDevice, nullptr);
    vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    vkDestroyInstance(instance, nullptr);
}

//基本资源 ////////////////////////////////////////////////////////////////////////////////////////////////////////

void VulkanRHIBackend::InitImGui(SDL_Window* window)
{
    initImGui = true;

    std::shared_ptr<VulkanRHIQueue> queue = ResourceCast(queues[QUEUE_TYPE_GRAPHICS][0]);

    //使用volk加载vulkan函数，需要重新绑定
    auto funcLoader = [](const char* funcName, void* engine)
        {
            auto backend = std::static_pointer_cast<VulkanRHIBackend>(EngineContext::RHI());
            PFN_vkVoidFunction instanceAddr = vkGetInstanceProcAddr(backend->GetInstance(), funcName);
            PFN_vkVoidFunction deviceAddr = vkGetDeviceProcAddr(backend->GetLogicalDevice(), funcName);
            return deviceAddr ? deviceAddr : instanceAddr;
        };
    // const bool funcsLoaded = ImGui_ImplVulkan_LoadFunctions(funcLoader, this);
    ImGui_ImplVulkan_LoadFunctions(VULKAN_VERSION, funcLoader, this);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    
    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.ApiVersion = VULKAN_VERSION;
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice;
    initInfo.Device = logicalDevice;
    initInfo.QueueFamily = queue->GetQueueFamilyIndex();
    initInfo.Queue = queue->GetHandle();
    initInfo.DescriptorPool = descriptorPool;
    // initInfo.Subpass = 0;
    initInfo.MinImageCount = FRAMES_IN_FLIGHT;
    initInfo.ImageCount = FRAMES_IN_FLIGHT;
    // initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;
    // initInfo.ColorAttachmentFormat = VulkanUtil::RHIFormatToVkFormat(EngineContext::Render()->GetColorFormat());

    initInfo.CheckVkResultFn = [](VkResult err) {
        if (err != VK_SUCCESS) LOG_FATAL("ImGui Vulkan error: %d", (int)err);
        };

    std::vector<VkFormat> colorFormats = { VulkanUtil::RHIFormatToVkFormat(EngineContext::Render()->GetColorFormat()) };

    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = colorFormats.data();

    //ImGui_ImplVulkan_LoadFunctions();
    //ImGui_ImplVulkan_Init(&initInfo, tempPass);
    //ImGui_ImplVulkan_Init(&initInfo, VK_NULL_HANDLE);
    ImGui_ImplVulkan_Init(&initInfo);
}

RHIQueueRef VulkanRHIBackend::GetQueue(const RHIQueueInfo& info) 
{
    return queues[info.type][info.index];
}

//RHISurfaceRef VulkanRHIBackend::CreateSurface(GLFWwindow* window)
//{
//    RHISurfaceRef surface = std::make_shared<VulkanRHISurface>(window, *this);
//    RegisterResource(surface);
//
//    return surface;
//}

RHISurfaceRef VulkanRHIBackend::CreateSurface(SDL_Window* window)
{
    RHISurfaceRef surface = std::make_shared<VulkanRHISurface>(window, *this);
    RegisterResource(surface);

    return surface;
}

RHISwapchainRef VulkanRHIBackend::CreateSwapChain(const RHISwapchainInfo& info)
{
    RHISwapchainRef swapchain = std::make_shared<VulkanRHISwapchain>(info, *this);
    RegisterResource(swapchain);

    return swapchain;
}

RHICommandPoolRef VulkanRHIBackend::CreateCommandPool(const RHICommandPoolInfo& info) 
{
    RHICommandPoolRef commandPool = std::make_shared<VulkanRHICommandPool>(info, *this);
    RegisterResource(commandPool);

    return commandPool;
}

RHICommandContextRef VulkanRHIBackend::CreateCommandContext(RHICommandPoolRef pool)
{
    RHICommandContextRef commandContext = std::make_shared<VulkanRHICommandContext>(pool, *this);
    RegisterResource(commandContext);

    return commandContext;
}

//缓冲，纹理，着色器，加速结构 ////////////////////////////////////////////////////////////////////////////////////////////////////////

RHIBufferRef VulkanRHIBackend::CreateBuffer(const RHIBufferInfo& info) 
{ 
    RHIBufferRef buffer = std::make_shared<VulkanRHIBuffer>(info, *this);
    RegisterResource(buffer);

    return buffer;
}

RHITextureRef VulkanRHIBackend::CreateTexture(const RHITextureInfo& info) 
{ 
    RHITextureRef texture = std::make_shared<VulkanRHITexture>(info, *this);
    RegisterResource(texture);

    return texture;
}

RHITextureViewRef VulkanRHIBackend::CreateTextureView(const RHITextureViewInfo& info)
{
    RHITextureViewRef textureView = std::make_shared<VulkanRHITextureView>(info, *this);
    RegisterResource(textureView);

    return textureView;
}

RHISamplerRef VulkanRHIBackend::CreateSampler(const RHISamplerInfo& info)
{
    RHISamplerRef sampler = std::make_shared<VulkanRHISampler>(info, *this);
    RegisterResource(sampler);

    return sampler;
}

RHIShaderRef VulkanRHIBackend::CreateShader(const RHIShaderInfo& info) 
{ 
    RHIShaderRef shader = std::make_shared<VulkanRHIShader>(info, *this);
    RegisterResource(shader);

    return shader;
}

RHIShaderBindingTableRef VulkanRHIBackend::CreateShaderBindingTable(const RHIShaderBindingTableInfo& info)
{
    RHIShaderBindingTableRef sbt = std::make_shared<VulkanRHIShaderBindingTable>(info, *this);
    RegisterResource(sbt);

    return sbt;
}

RHITopLevelAccelerationStructureRef VulkanRHIBackend::CreateTopLevelAccelerationStructure(const RHITopLevelAccelerationStructureInfo& info)
{
    RHITopLevelAccelerationStructureRef tlas = std::make_shared<VulkanRHITopLevelAccelerationStructure>(info, *this);
    RegisterResource(tlas);

    return tlas;
}

RHIBottomLevelAccelerationStructureRef VulkanRHIBackend::CreateBottomLevelAccelerationStructure(const RHIBottomLevelAccelerationStructureInfo& info)
{
    RHIBottomLevelAccelerationStructureRef blas = std::make_shared<VulkanRHIBottomLevelAccelerationStructure>(info, *this);
    RegisterResource(blas);

    return blas;
}

//根签名，描述符 ////////////////////////////////////////////////////////////////////////////////////////////////////////

RHIRootSignatureRef VulkanRHIBackend::CreateRootSignature(const RHIRootSignatureInfo& info) 
{ 
    RHIRootSignatureRef rootSignature = std::make_shared<VulkanRHIRootSignature>(info, *this);
    RegisterResource(rootSignature);

    return rootSignature;
}

//管线状态 ////////////////////////////////////////////////////////////////////////////////////////////////////////

RHIGraphicsPipelineRef VulkanRHIBackend::CreateGraphicsPipeline(const RHIGraphicsPipelineInfo& info) 
{ 
    RHIGraphicsPipelineRef graphicsPipeline = std::make_shared<VulkanRHIGraphicsPipeline>(info, *this);
    RegisterResource(graphicsPipeline);

    return graphicsPipeline;
}

RHIComputePipelineRef VulkanRHIBackend::CreateComputePipeline(const RHIComputePipelineInfo& info) 
{ 
    RHIComputePipelineRef computePipeline = std::make_shared<VulkanRHIComputePipeline>(info, *this);
    RegisterResource(computePipeline);

    return computePipeline;
}

RHIRayTracingPipelineRef VulkanRHIBackend::CreateRayTracingPipeline(const RHIRayTracingPipelineInfo& info) 
{
    RHIRayTracingPipelineRef rayTracingPipeline = std::make_shared<VulkanRHIRayTracingPipeline>(info, *this);
    RegisterResource(rayTracingPipeline);

    return rayTracingPipeline;
}

//同步 ////////////////////////////////////////////////////////////////////////////////////////////////////////

RHIFenceRef VulkanRHIBackend::CreateFence(bool signaled) 
{
    RHIFenceRef fence = std::make_shared<VulkanRHIFence>(signaled, *this);
    RegisterResource(fence);

    return fence;
}

RHISemaphoreRef VulkanRHIBackend::CreateSemaphore(bool isTimeline, uint64_t initialValue)
{
    RHISemaphoreRef semaphore = std::make_shared<VulkanRHISemaphore>(*this, isTimeline, initialValue);   // timeline
    RegisterResource(semaphore);

    return semaphore;
}

//立即模式的命令接口 ////////////////////////////////////////////////////////////////////////////////////////////////////////

RHICommandListImmediateRef VulkanRHIBackend::GetImmediateCommand() 
{
    return immediateCommand;
}

std::shared_ptr<VulkanRHICommandContextImmediate> VulkanRHIBackend::GetImmediateCommandContest()  
{
    return dynamic_pointer_cast<VulkanRHICommandContextImmediate>(immediateCommandContext); 
}

void VulkanRHIBackend::CreateInstance()
{
    //使用volk库来做vulkan函数引入，先初始化
    if (volkInitialize() != VK_SUCCESS) {
        LOG_FATAL("Volk initialize failed!");
        return;
    }

    //instance的layer信息
    {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        availableLayers = std::vector<VkLayerProperties>(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (auto& layer : availableLayers)
        {
            std::cout << layer.layerName << " " << layer.description << " " << layer.specVersion << " " << layer.implementationVersion << std::endl;
        }
        std::cout << " " << std::endl;
    }

    // 初始化VkApplicationInfo
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Toy Render Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VULKAN_VERSION;    //使用的Vulkan API版本号

    // 初始化VkInstanceCreateInfo，全局信息？
    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto validationLayers = VulkanUtil::GetDebugMessengerCreateInfo();
    auto validationFeature = VulkanUtil::GetValidationFeatureCreateInfo();
    std::vector<const char*> layers;
    if (backendInfo.enableDebug)   // 验证层,不做检查了
    {             
        for (auto& layer : INSTANCE_LAYERS) layers.push_back(layer);

        createInfo.enabledLayerCount = (uint32_t)layers.size();
        createInfo.ppEnabledLayerNames = layers.data();

        // debug扩展信息
        createInfo.pNext = &validationLayers;

        // 设置验证层支持的特性，有的是默认没开启的，比如对同步的验证
        validationLayers.pNext = &validationFeature;
    }

    // 扩展支持信息，不做检查了
    auto instanceExtentions = VulkanUtil::GetRequiredInstanceExtensions(backendInfo.enableDebug);
    createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtentions.size());
    createInfo.ppEnabledExtensionNames = instanceExtentions.data();

    // 创建Vulkan实例
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) 
    {
        LOG_FATAL("Failed to create instance!");
    }

    //volk初始化
    volkLoadInstance(instance); 
    
    //创建DebugMessager
    if (backendInfo.enableDebug)
    {
        VkDebugUtilsMessengerCreateInfoEXT info = VulkanUtil::GetDebugMessengerCreateInfo();
        if (vkCreateDebugUtilsMessengerEXT(instance, &info, nullptr, &debugMessenger) != VK_SUCCESS) 
        {
            LOG_FATAL("Failed to set up debug messenger!");
        }
    }
}

void VulkanRHIBackend::CreatePhysicalDevice()
{
    // 选择支持的硬件
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (auto& device : devices) {
        vkGetPhysicalDeviceProperties(device, &properties);
        for (auto target : TARGET_DEVICES)
        {
            std::string name = std::string(properties.deviceName);
            //std::transform(name.begin(), name.end(), name.begin(), std::toupper);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return std::toupper(c); });


            if (name.find(target) != std::string::npos)    //直接用名称查找了，不检查各种要求
            {
                //基本信息
                {
                    std::cout << " " << std::endl;
                    std::cout << properties.deviceName << std::endl;

                    std::cout << "framebufferColorSampleCounts : " << properties.limits.framebufferColorSampleCounts << std::endl;
                    std::cout << "framebufferDepthSampleCounts : " << properties.limits.framebufferDepthSampleCounts << std::endl;
                    std::cout << "framebufferStencilSampleCounts : " << properties.limits.framebufferStencilSampleCounts << std::endl;
                    std::cout << "maxColorAttachments : " << properties.limits.maxColorAttachments << std::endl;
                    std::cout << "maxDescriptorSetInputAttachments : " << properties.limits.maxDescriptorSetInputAttachments << std::endl;
                    std::cout << "maxDescriptorSetUniformBuffers : " << properties.limits.maxDescriptorSetUniformBuffers << std::endl;
                    std::cout << "maxFramebufferLayers : " << properties.limits.maxFramebufferLayers << std::endl;
                    std::cout << "maxPushConstantsSize : " << properties.limits.maxPushConstantsSize << std::endl;
                    std::cout << "maxBoundDescriptorSets : " << properties.limits.maxBoundDescriptorSets << std::endl;
                    std::cout << "maxComputeWorkGroupInvocations : " << properties.limits.maxComputeWorkGroupInvocations << std::endl;
                    std::cout << "minStorageBufferOffsetAlignment : " << properties.limits.minStorageBufferOffsetAlignment << std::endl;
                    
                }

                //初始化存储物理设备支持的各种属性，特性，扩展，内存特性，队列信息
                //vkGetPhysicalDeviceFeatures2(device, &fetures2);
                vkGetPhysicalDeviceFeatures(device, &features);
                vkGetPhysicalDeviceMemoryProperties(device, &memoryProperties);
                //for (int i = 0; i < memoryProperties.memoryTypeCount; i++)
                //{
                //    std::cout << "\t" << memoryProperties.memoryTypes[i].propertyFlags << std::endl;
                //}

                uint32_t queueFamilyCount = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
                queueFamilyProperties.resize(queueFamilyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilyProperties.data());

                uint32_t extCount = 0;
                vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
                std::vector<VkExtensionProperties> extensions(extCount);
                if (vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, &extensions.front()) == VK_SUCCESS)
                {
                    std::cout << " " << std::endl;
                    for (auto& ext : extensions)
                    {
                        supportedExtensions.push_back(ext.extensionName);
                        std::cout << ext.extensionName << std::endl;
                    }
                }

                if(backendInfo.enableRayTracing)
                {
                    //获取光追管线特性
                    rayTracingPipelineProperties = {};
                    rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

                    VkPhysicalDeviceProperties2 deviceProperties2 = {};
                    deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
                    deviceProperties2.pNext = &rayTracingPipelineProperties;
                    vkGetPhysicalDeviceProperties2(device, &deviceProperties2);
                }

                physicalDevice = device;
                return;
            }
        }
    }
    
    LOG_FATAL("Target device not found!");
}

void VulkanRHIBackend::CreateLogicalDevice()
{
    // 获取队列族信息
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    queueFamilyProperties.resize(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

    for(auto& index : queueIndices) index = -1;

    std::cout << " " << std::endl;
    for (auto& queueFamily : queueFamilyProperties)
    {
        std::cout << "Queue count: " << queueFamily.queueCount << std::endl;
        std::cout << "Queue flags: " << VulkanUtil::QueueFlagsToString(queueFamily.queueFlags) << std::endl;
    }

    // 队列族选择：两遍策略
    //   phase1 优先专用的族——compute/transfer 尽量避开 graphics 族/全能族，让各类型拿到 族内独立的 VkQueue
    // 
    //   phase2 回落——带对应能力位且还有空位的任意族（含 graphics 族/全能族）
    //   同族多类型各自保留 MAX_QUEUE_CNT 个请求（requestedCounts 累计后 clamp 到族队列数，
    //   不足时 CreateQueues 按已请求数取模别名——合法，仅串行化，且同族判定会抑制release/acquire）
    std::vector<uint32_t> requestedCounts(queueFamilyProperties.size(), 0);
    std::vector<int32_t> freeCounts(queueFamilyProperties.size());
    for (size_t i = 0; i < queueFamilyProperties.size(); i++)
        freeCounts[i] = static_cast<int32_t>(queueFamilyProperties[i].queueCount);

    // require: 必备能力位；
    // avoidMask: 专用的判定——族不带这些能力位才算专用的，为0则不限制
    // avoidGraphicsFamily: 额外避开 graphics 族（须先选定 graphics）
    auto pickFamily = [&](VkQueueFlags require, VkQueueFlags avoidMask, bool avoidGraphicsFamily) -> int32_t
    {
        int32_t best = -1;
        for (int32_t i = 0; i < static_cast<int32_t>(queueFamilyProperties.size()); i++)
        {
            const VkQueueFamilyProperties& family = queueFamilyProperties[i];
            if (!(family.queueFlags & require)) continue;
            if (freeCounts[i] <= 0) continue;
            if (avoidGraphicsFamily && queueIndices[QUEUE_TYPE_GRAPHICS] == i) continue;
            if (avoidMask && (family.queueFlags & avoidMask)) continue;
            if (best < 0 || freeCounts[i] > freeCounts[best]) best = i;
        }
        if (best >= 0)
            freeCounts[best] = std::max(0, freeCounts[best] - MAX_QUEUE_CNT);
        return best;
    };

    // pass1：专用的 优先
    queueIndices[QUEUE_TYPE_GRAPHICS] = pickFamily(VK_QUEUE_GRAPHICS_BIT, 0, false);
    queueIndices[QUEUE_TYPE_COMPUTE]  = pickFamily(VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT, true);
    queueIndices[QUEUE_TYPE_TRANSFER] = pickFamily(VK_QUEUE_TRANSFER_BIT,
                                        static_cast<VkQueueFlagBits>(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT), true);

    // pass2：回落
    if (queueIndices[QUEUE_TYPE_COMPUTE]  < 0) queueIndices[QUEUE_TYPE_COMPUTE]  = pickFamily(VK_QUEUE_COMPUTE_BIT, 0, false);
    if (queueIndices[QUEUE_TYPE_TRANSFER] < 0) queueIndices[QUEUE_TYPE_TRANSFER] = pickFamily(VK_QUEUE_TRANSFER_BIT, 0, false);

    if (queueIndices[QUEUE_TYPE_GRAPHICS] < 0) LOG_FATAL("Fail to allocate graphics queue!");
    // 带GRAPHICS位的族必然带COMPUTE/TRANSFER位，走到这里说明没有可用队列，复用graphics族
    if (queueIndices[QUEUE_TYPE_COMPUTE]  < 0) queueIndices[QUEUE_TYPE_COMPUTE]  = queueIndices[QUEUE_TYPE_GRAPHICS];
    if (queueIndices[QUEUE_TYPE_TRANSFER] < 0) queueIndices[QUEUE_TYPE_TRANSFER] = queueIndices[QUEUE_TYPE_GRAPHICS];

    // 每类型向所属族保留 MAX_QUEUE_CNT 个请求
    for (int type = 0; type < QUEUE_TYPE_MAX_ENUM; type++)
        requestedCounts[queueIndices[type]] += MAX_QUEUE_CNT;

    allocatedQueueCounts.resize(queueFamilyProperties.size());
    std::vector<uint32_t> allocatedCounts(queueFamilyProperties.size());
    for (size_t i = 0; i < queueFamilyProperties.size(); i++)
    {
        allocatedCounts[i] = std::min(requestedCounts[i], queueFamilyProperties[i].queueCount);
        allocatedQueueCounts[i] = allocatedCounts[i];    // CreateQueues 的取模上限（见下）
    }

    // 好像graphics queue就支持了？不需要单独处理？
    // // 窗口支持
    // VkBool32 presentSupport = false;
    // vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
    // if (queueFamily.queueCount > 0 && presentSupport && presentFamily < 0 && graphicsFamily != i) { //强行使用不同的queue
    //     presentFamily = i;
    // }

    // 创建设备请求信息
    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    //队列信息
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    for(int i = 0; i < allocatedCounts.size(); i++)
    {
        if(allocatedCounts[i] == 0) continue;

        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = i;                               //队列族
        queueCreateInfo.queueCount = allocatedCounts[i];                    //队列数目，多个队列可以支持并行异步计算
        queueCreateInfo.pQueuePriorities = QUEUE_PRIORITIES;                //优先级
        queueCreateInfos.push_back(queueCreateInfo);
    }
    createInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();   
    createInfo.pQueueCreateInfos = queueCreateInfos.data();

    //扩展信息
    std::vector<const char*> deviceExtentions;
    for (auto extention : DEVICE_EXTENTIONS) deviceExtentions.push_back(extention);
    if (backendInfo.enableRayTracing) { for (auto extention : RAY_TRACING_DEVICE_EXTENTIONS) deviceExtentions.push_back(extention); }
    // mesh shader扩展仅在设备支持时启用（supportedExtensions来自物理设备枚举；不支持时stageFlags-parameter错误会保留）
    for (auto extention : MESH_SHADER_DEVICE_EXTENTIONS)
        for (auto& supported : supportedExtensions)
            if (supported == extention) { deviceExtentions.push_back(extention); break; }
    createInfo.enabledExtensionCount = (uint32_t)deviceExtentions.size(); 
    createInfo.ppEnabledExtensionNames = deviceExtentions.data();

    //验证层信息
    std::vector<const char*> devicelayers;
    if (backendInfo.enableDebug)
    {           
        for (auto layer : DEVICE_LAYERS) devicelayers.push_back(layer);

        createInfo.enabledLayerCount = (uint32_t)devicelayers.size(); 
        createInfo.ppEnabledLayerNames = devicelayers.data();
    }

    // 特性支持
    VkPhysicalDeviceFeatures deviceFeatures = {};       //设备特性信息
    deviceFeatures.samplerAnisotropy = VK_TRUE;         //请求各向异性采样支持
    deviceFeatures.geometryShader = VK_TRUE;            //几何着色器支持
    deviceFeatures.tessellationShader = VK_TRUE;        //曲面细分着色器支持
    deviceFeatures.pipelineStatisticsQuery = VK_TRUE;   //管线统计查询支持
    deviceFeatures.fillModeNonSolid = VK_TRUE;          //线框模式
    deviceFeatures.multiDrawIndirect = VK_TRUE;         //多重间接绘制
    deviceFeatures.independentBlend = VK_TRUE;          //MRT单独设置每个混合状态
    deviceFeatures.drawIndirectFirstInstance = VK_TRUE; //允许间接绘制的firstInstance不为0
    deviceFeatures.shaderInt64 = VK_TRUE;               //64位支持
    deviceFeatures.shaderFloat64 = VK_TRUE;
    deviceFeatures.wideLines = VK_TRUE;                 //线框模式的宽度
    createInfo.pEnabledFeatures = &deviceFeatures;

    VkPhysicalDeviceVulkan12Features vulkan12Features{};                                        //1.2版本的其他支持
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.samplerFilterMinmax = VK_TRUE;                                             //采样器的过滤模式，用于Hiz
    vulkan12Features.timelineSemaphore = VK_TRUE;                                               //时间线信号量（RDG多队列跨队列同步用，1.2核心）
    vulkan12Features.hostQueryReset = VK_TRUE;                                                  //host侧重置查询池（诊断时间戳复用，1.2核心）
                                                                                                //bindless支持，描述符索引
    vulkan12Features.runtimeDescriptorArray = VK_TRUE;                                          //SPIR-V 中使用动态数组
    vulkan12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;                        //DescriptorSet Layout 的 Binding 中使用可变大小的 AoD
    vulkan12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;                       //SPIR-V 中通过 nonuniformEXT Decoration 用于非 Uniform 变量下标索引资源数组
    vulkan12Features.descriptorBindingPartiallyBound = VK_TRUE;
    vulkan12Features.descriptorIndexing = VK_TRUE;
    vulkan12Features.bufferDeviceAddress = VK_TRUE;
    createInfo.pNext = &vulkan12Features;

	VkPhysicalDeviceVulkan13Features vulkan13Features{};                                        //1.3版本的其他支持
	vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	vulkan13Features.dynamicRendering = VK_TRUE;                                                // 动态渲染
    vulkan13Features.synchronization2 = VK_TRUE;                                                // 
    vulkan13Features.maintenance4 = VK_TRUE;                                                    //         
	vulkan12Features.pNext = &vulkan13Features;

    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT dynamicVertexInputFeatures = {};         //动态顶点输入描述
    dynamicVertexInputFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT;
    dynamicVertexInputFeatures.vertexInputDynamicState = VK_TRUE;
    vulkan13Features.pNext = &dynamicVertexInputFeatures;

    VkPhysicalDeviceMultiviewFeaturesKHR mulitiViewFeatures = {};                                  //多视口
    mulitiViewFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES_KHR;
    mulitiViewFeatures.multiview = VK_TRUE;
    mulitiViewFeatures.multiviewGeometryShader = VK_TRUE;
    mulitiViewFeatures.multiviewTessellationShader = VK_TRUE;
    dynamicVertexInputFeatures.pNext = &mulitiViewFeatures;

    //这个扩展已经在1.2提为core特性，可以放在VkPhysicalDeviceVulkan12Features里声明   //最开始是 VkPhysicalDeviceDescriptorIndexingFeaturesEXT
    //VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{};                          //bindless支持，描述符索引
    //indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    //indexingFeatures.runtimeDescriptorArray = VK_TRUE;                      //SPIR-V 中使用动态数组
    //indexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;    //DescriptorSet Layout 的 Binding 中使用可变大小的 AoD
    //indexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;   //SPIR-V 中通过 nonuniformEXT Decoration 用于非 Uniform 变量下标索引资源数组
    //indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
    //rayTracingPipelineFeatures.pNext = &indexingFeatures;

    //VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT divisorFeatures = {};               //请求逐实例参数支持
    //divisorFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT;
    //divisorFeatures.vertexAttributeInstanceRateDivisor = VK_TRUE;
    //divisorFeatures.vertexAttributeInstanceRateZeroDivisor = VK_TRUE;
    //createInfo.pNext = &divisorFeatures;    //申请新特性（API1.0之后的）支持，链式申请

    VkPhysicalDeviceBufferDeviceAddressFeaturesEXT deviceAddressfeture = {};                //请求内存地址信息
    deviceAddressfeture.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT;
    deviceAddressfeture.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures = {};    //rt加速结构
    accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelerationStructureFeatures.accelerationStructure = VK_TRUE;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures = {};          //rt管线
    rayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;

    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {};                              //rt查询
    rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rayQueryFeatures.rayQuery = VK_TRUE;

    if(backendInfo.enableRayTracing)
    {
        mulitiViewFeatures.pNext = &deviceAddressfeture;    
        deviceAddressfeture.pNext = &accelerationStructureFeatures;
        accelerationStructureFeatures.pNext = &rayTracingPipelineFeatures;
        rayTracingPipelineFeatures.pNext = &rayQueryFeatures;
    }

    VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &logicalDevice);
    if (result != VK_SUCCESS) 
    {
        LOG_FATAL("Failed to create logical device! [%d]", result);
    }

    volkLoadDevice(logicalDevice);  //volk
    //volkLoadDeviceTable(struct VolkDeviceTable* table, VkDevice device);    //volk
}

void VulkanRHIBackend::CreateQueues()
{
    // offsets 必须按【族】索引（原实现按队列类型索引——三类型映射到同族时会取到同一批
    // VkQueue句柄，异步队列与graphics队列完全别名）。同族多类型各自取得族内不同的队列索引；
    // 族内请求队列数不足时按 allocatedQueueCounts 取模别名（合法，仅串行化，且族相等会正确抑制跨队列所有权转移的发射）
    
    // 每个队列族各自已分配的队列数，同时也是该族下一次分配队列的索引
    std::vector<uint32_t> offsets(queueFamilyProperties.size(), 0);
    for (uint32_t i = 0; i < QUEUE_TYPE_MAX_ENUM; i++)
    {
        for(uint32_t j = 0; j < MAX_QUEUE_CNT; j++)
        {
            const uint32_t family = queueIndices[i];
            // 限制在 allocatedQueueCounts[family] 内循环取模，保证超分时同族内取别名
            const uint32_t slot = offsets[family] % allocatedQueueCounts[family];
            offsets[family]++;

            VkQueue queue;
            vkGetDeviceQueue(logicalDevice, family, slot, &queue);

            RHIQueueInfo info =
            {
                .type = (QueueType)i,
                .index = j,
            };
            queues[i][j] = std::make_shared<VulkanRHIQueue>(info, queue, family);
            RegisterResource(queues[i][j]);
        }
    }
}

void VulkanRHIBackend::CreateMemoryAllocator()
{
    // volk集成vma: https://zhuanlan.zhihu.com/p/634912614 
    // vma官方文档: https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/index.html
    // 对照该表，不同vulkan版本有不同的绑定要求: https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/struct_vma_vulkan_functions.html

    VmaVulkanFunctions vulkanFunctions{};
    vulkanFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
    vulkanFunctions.vkAllocateMemory = vkAllocateMemory;
    vulkanFunctions.vkFreeMemory = vkFreeMemory;
    vulkanFunctions.vkMapMemory = vkMapMemory;
    vulkanFunctions.vkUnmapMemory = vkUnmapMemory;
    vulkanFunctions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
    vulkanFunctions.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
    vulkanFunctions.vkBindBufferMemory = vkBindBufferMemory;
    vulkanFunctions.vkBindImageMemory = vkBindImageMemory;
    vulkanFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;  
    vulkanFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    vulkanFunctions.vkCreateBuffer = vkCreateBuffer;
    vulkanFunctions.vkDestroyBuffer = vkDestroyBuffer;
    vulkanFunctions.vkCreateImage = vkCreateImage;
    vulkanFunctions.vkDestroyImage = vkDestroyImage;
    vulkanFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;
    // vulkanFunctions.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2KHR;
    // vulkanFunctions.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2KHR;
    // vulkanFunctions.vkBindBufferMemory2KHR = vkBindBufferMemory2KHR;
    // vulkanFunctions.vkBindImageMemory2KHR = vkBindImageMemory2KHR;
    // vulkanFunctions.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2KHR;
    vulkanFunctions.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2;
    vulkanFunctions.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2;
    vulkanFunctions.vkBindBufferMemory2KHR = vkBindBufferMemory2;
    vulkanFunctions.vkBindImageMemory2KHR = vkBindImageMemory2;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
    vulkanFunctions.vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements;  
    vulkanFunctions.vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements;

    VmaAllocatorCreateInfo allocatorCreateInfo = {};
    allocatorCreateInfo.vulkanApiVersion = VULKAN_VERSION;
    allocatorCreateInfo.physicalDevice = physicalDevice;
    allocatorCreateInfo.device = logicalDevice;
    allocatorCreateInfo.instance = instance;
    allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
    allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; // 加上这个才能获取地址

    vmaCreateAllocator(&allocatorCreateInfo, &memoryAllocator);
}

void VulkanRHIBackend::CreateDescriptorPool()
{

    std::vector<VkDescriptorPoolSize> descriptorPoolSizes = {
          { VK_DESCRIPTOR_TYPE_SAMPLER,                     4096 },
          { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,      4096 },
          { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,               4096 },
          { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,               4096 },
          { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,        4096 },
          { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,        4096 },
          { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,              4096 },
          { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,              4096 },
          { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,      4096 },
          { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,      4096 },
    };

    //描述符池信息
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size());
    poolInfo.pPoolSizes = descriptorPoolSizes.data();
    poolInfo.maxSets = 8192;               //指定最大描述符集合数
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;   //TODO 使得描述符可以实时更新

    if (vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) 
    {
        LOG_FATAL("Failed to create descriptor pool!");
    }
}

void VulkanRHIBackend::CreateImmediateCommand()
{
    immediateCommandContext = std::make_shared<VulkanRHICommandContextImmediate>(*this);
    RegisterResource(immediateCommandContext);

    CommandListImmediateInfo info = {
        .context = immediateCommandContext
    };
    immediateCommand = std::make_shared<RHICommandListImmediate>(info);
}


void TextureBarrier(VkCommandBuffer commandBuffer, const RHITextureBarrier& barrier, uint32_t queueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED)
{
    TextureSubresourceRange range = barrier.subresource;
    if (range.aspect == TEXTURE_ASPECT_NONE) range = barrier.texture->GetDefaultSubresourceRange();

    // queueFlags：IGNORED→图形族位；否则取录制队列族能力位。
    // 推导函数本身已按队列能力裁剪stage/access：非图形族上不会带图形专属stage
    // （如compute族命令缓冲里SRV读只落COMPUTE/RT，不带VERTEX/FRAGMENT位）
    const VkQueueFlags queueFlags = queueFamilyIndex != RHI_QUEUE_FAMILY_IGNORED
        ? Backend()->GetQueueFamilyProperties()[queueFamilyIndex].queueFlags
        : VK_QUEUE_GRAPHICS_BIT;

    VkAccessFlags srcAccessMask = VulkanUtil::ResourceStateToAccessFlags(barrier.srcState, queueFlags);
    VkAccessFlags dstAccessMask = VulkanUtil::ResourceStateToAccessFlags(barrier.dstState, queueFlags);
    VkPipelineStageFlags srcStage = VulkanUtil::AccessFlagsToPipelineStageFlags(srcAccessMask, queueFlags);
    VkPipelineStageFlags dstStage = VulkanUtil::AccessFlagsToPipelineStageFlags(dstAccessMask, queueFlags);

    // srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;   // 可以保证绝对不会出错
    // dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;   // 目前验证层VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT还是会有一些报错，太难调了

    // UNDEFINED源侧（帧首UNDEFINED→X转移）：srcAccess=NONE不与上一帧的写建立内存依赖——
    // 布局合法（丢弃语义）但sync validation实证为WAW缺口（上帧Editor UI写深度 vs 本帧Depth Copy
    // 帧首转移）。保守全量：等待之前全部工作再执行转移
    if (barrier.srcState == RESOURCE_STATE_UNDEFINED)
    {
        srcStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    }
    // PRESENT侧（转入PRESENT布局）：dstAccess=NONE经空access回落得TOP_OF_PIPE——转移完成点
    // 早于一切后续（含present引擎读取）→PRESENT_AFTER_WRITE缺口。规范形态=BOTTOM_OF_PIPE；
    // MEMORY读写双位保证对呈现引擎的写可用性（sync validation实证单READ位不足）
    if (barrier.dstState == RESOURCE_STATE_PRESENT)
    {
        dstStage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    }
    if (barrier.srcState == RESOURCE_STATE_PRESENT)
    {
        srcStage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    }
    // 布局转移链的写-写衔接：转移本身=写，屏障src/dst一律带MEMORY读写位——同态acquire
    // （SRV→SRV）的srcAccess=SHADER_READ-only不覆盖上一转移写的可用性（sync validation
    // 实证：Depth的release转移链WRITE_RACING_WRITE）。图像屏障本就是全图可用性操作，
    // 保守全量无可测代价
    srcAccessMask |= VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    dstAccessMask |= VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

    VkImageMemoryBarrier memoryBarrier = {};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    memoryBarrier.srcQueueFamilyIndex = barrier.srcQueueFamily == RHI_QUEUE_FAMILY_IGNORED ? VK_QUEUE_FAMILY_IGNORED : barrier.srcQueueFamily;
    memoryBarrier.dstQueueFamilyIndex = barrier.dstQueueFamily == RHI_QUEUE_FAMILY_IGNORED ? VK_QUEUE_FAMILY_IGNORED : barrier.dstQueueFamily;
    memoryBarrier.oldLayout = VulkanUtil::ResourceStateToImageLayout(barrier.srcState);
    memoryBarrier.newLayout = VulkanUtil::ResourceStateToImageLayout(barrier.dstState);
    memoryBarrier.image = ResourceCast(barrier.texture)->GetHandle();
    memoryBarrier.subresourceRange = VulkanUtil::SubresourceToVk(range);
    memoryBarrier.srcAccessMask = srcAccessMask;
    memoryBarrier.dstAccessMask = dstAccessMask;

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage, dstStage, 0,
        0, nullptr,
        0, nullptr,
        1, &memoryBarrier);

}

void BufferBarrier(VkCommandBuffer commandBuffer, const RHIBufferBarrier& barrier, uint32_t queueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED)
{
    // queueFlags：IGNORED→图形族位（图形族直通=不裁剪）；否则取录制队列族能力位（同TextureBarrier）
    const VkQueueFlags queueFlags = queueFamilyIndex != RHI_QUEUE_FAMILY_IGNORED
        ? Backend()->GetQueueFamilyProperties()[queueFamilyIndex].queueFlags
        : VK_QUEUE_GRAPHICS_BIT;

    VkAccessFlags srcAccessMask = VulkanUtil::ResourceStateToAccessFlags(barrier.srcState, queueFlags);
    VkAccessFlags dstAccessMask = VulkanUtil::ResourceStateToAccessFlags(barrier.dstState, queueFlags);
    VkPipelineStageFlags srcStage = VulkanUtil::AccessFlagsToPipelineStageFlags(srcAccessMask, queueFlags);
    VkPipelineStageFlags dstStage = VulkanUtil::AccessFlagsToPipelineStageFlags(dstAccessMask, queueFlags);

    // UNDEFINED源侧与纹理同理（sync validation实证的WAW缺口——帧首转移须与上帧写建立内存依赖）
    if (barrier.srcState == RESOURCE_STATE_UNDEFINED)
    {
        srcStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    }

    VkBufferMemoryBarrier memoryBarrier = {};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    memoryBarrier.srcQueueFamilyIndex = barrier.srcQueueFamily == RHI_QUEUE_FAMILY_IGNORED ? VK_QUEUE_FAMILY_IGNORED : barrier.srcQueueFamily;
    memoryBarrier.dstQueueFamilyIndex = barrier.dstQueueFamily == RHI_QUEUE_FAMILY_IGNORED ? VK_QUEUE_FAMILY_IGNORED : barrier.dstQueueFamily;
    memoryBarrier.srcAccessMask = srcAccessMask;
    memoryBarrier.dstAccessMask = dstAccessMask;
    memoryBarrier.buffer = ResourceCast(barrier.buffer)->GetHandle();
    memoryBarrier.offset = barrier.offset;               // TODO
    memoryBarrier.size = barrier.size == 0 ? VK_WHOLE_SIZE : barrier.size;

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage, dstStage, 0,
        0, nullptr,
        1, &memoryBarrier,
        0, nullptr);
}

void CopyTextureToBuffer(VkCommandBuffer commandBuffer, RHITextureRef src, TextureSubresourceLayers srcSubresource, RHIBufferRef dst, uint64_t dstOffset)
{
    VkBufferImageCopy copy = {};
    copy.bufferOffset = dstOffset;
    copy.bufferRowLength = 0;       // TODO
    copy.bufferImageHeight = 0;
    copy.imageSubresource = VulkanUtil::SubresourceToVk(srcSubresource);
    copy.imageOffset = {0, 0, 0};
    copy.imageExtent = VulkanUtil::ExtentToVk(src->MipExtent(srcSubresource.mipLevel));

    vkCmdCopyImageToBuffer(commandBuffer,
    ResourceCast(src)->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    ResourceCast(dst)->GetHandle(),
    1, &copy);
}

void CopyBufferToTexture(VkCommandBuffer commandBuffer, RHIBufferRef src, uint64_t srcOffset, RHITextureRef dst, TextureSubresourceLayers dstSubresource)
{
    VkBufferImageCopy copy = {};
    copy.bufferOffset = srcOffset;
    copy.bufferRowLength = 0;       // TODO
    copy.bufferImageHeight = 0;
    copy.imageSubresource = VulkanUtil::SubresourceToVk(dstSubresource);
    copy.imageOffset = {0, 0, 0};
    copy.imageExtent = VulkanUtil::ExtentToVk(dst->MipExtent(dstSubresource.mipLevel));

    vkCmdCopyBufferToImage(commandBuffer, 
    ResourceCast(src)->GetHandle(),
    ResourceCast(dst)->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
    1, &copy);
}

void CopyBuffer(VkCommandBuffer commandBuffer, RHIBufferRef src, uint64_t srcOffset, RHIBufferRef dst, uint64_t dstOffset, uint64_t size)
{
    VkBufferCopy copy = {};
    copy.srcOffset = srcOffset;
    copy.dstOffset = dstOffset;
    copy.size = size;

    vkCmdCopyBuffer(commandBuffer,
    ResourceCast(src)->GetHandle(),
    ResourceCast(dst)->GetHandle(),
    1, &copy);
}

void CopyTexture(VkCommandBuffer commandBuffer, RHITextureRef src, TextureSubresourceLayers srcSubresource, RHITextureRef dst, TextureSubresourceLayers dstSubresource)
{
    VkImageCopy imageCopy = {};
    imageCopy.srcOffset = {0, 0, 0};    // TODO ?
    imageCopy.dstOffset = {0, 0, 0};
    imageCopy.srcSubresource = (srcSubresource.aspect == 0) ? VulkanUtil::SubresourceToVk(src->GetDefaultSubresourceLayers()) : VulkanUtil::SubresourceToVk(srcSubresource);
    imageCopy.dstSubresource = (dstSubresource.aspect == 0) ? VulkanUtil::SubresourceToVk(dst->GetDefaultSubresourceLayers()) : VulkanUtil::SubresourceToVk(dstSubresource);
    imageCopy.extent = VulkanUtil::ExtentToVk(src->MipExtent(srcSubresource.mipLevel));

    vkCmdCopyImage(commandBuffer,
        ResourceCast(src)->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        ResourceCast(dst)->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &imageCopy);

        
}

// 假定处于正确的src在src状态，dst在dst状态
void BlitTexture(   VkCommandBuffer commandBuffer, 
                    RHITextureRef src, RHITextureRef dst, 
                    TextureSubresourceLayers srcSubresource, TextureSubresourceLayers dstSubresource, 
                    FilterType filter)
{
    VkImageSubresourceLayers srcLayer = VulkanUtil::SubresourceToVk(srcSubresource);
    VkImageSubresourceLayers dstLayer = VulkanUtil::SubresourceToVk(dstSubresource);

    uint32_t srcMip = srcSubresource.mipLevel;
    uint32_t dstMip = dstSubresource.mipLevel;

    VkImageBlit blit = {};
    blit.srcOffsets[0] = {0, 0, 0}; //TODO offset
    blit.srcOffsets[1] = {  (int32_t)(src->GetInfo().extent.width / pow(2, srcMip)),
                            (int32_t)(src->GetInfo().extent.height / pow(2, srcMip)), 1};
    blit.srcSubresource = srcLayer;

    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {  (int32_t)(dst->GetInfo().extent.width / pow(2, dstMip)),
                            (int32_t)(dst->GetInfo().extent.height / pow(2, dstMip)), 1};
    blit.dstSubresource = dstLayer;

    vkCmdBlitImage(commandBuffer,
        ResourceCast(src)->GetHandle(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        ResourceCast(dst)->GetHandle(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &blit,
        VulkanUtil::FilterTypeToVk(filter));
}

// 假定起始时各层均为transfer src状态，结束时也为src状态
void GenerateMips(VkCommandBuffer commandBuffer, RHITextureRef src)
{
    //总计生成的mip层数
    uint32_t mipLevels = src->GetInfo().mipLevels;  
    if(mipLevels <= 1) return; 

    VkImageSubresourceRange transition = {};
    transition.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    transition.baseMipLevel = 0;
    transition.levelCount = 1;
    transition.baseArrayLayer = 0;
    transition.layerCount = 1;

    for(uint32_t i = 0; i < src->GetInfo().arrayLayers; i++)
    {
        transition.baseMipLevel = 0;
        transition.baseArrayLayer = i;

        // 先将后面的层全设置到dst
        TextureBarrier(commandBuffer, 
        {src, 
        RESOURCE_STATE_TRANSFER_SRC, RESOURCE_STATE_TRANSFER_DST, 
                {TEXTURE_ASPECT_COLOR, 1, mipLevels - 1, transition.baseArrayLayer, transition.layerCount}});

        //循环生成各级mip，并将对应层级转到srcLayout
        for (uint32_t i = 1; i < mipLevels; i++)    //总共mipLevels级，只需要mipLevels-1次blit
        {   
            BlitTexture(
                commandBuffer, 
                src, 
                src, 
                { transition.aspectMask, transition.baseMipLevel, transition.baseArrayLayer, transition.layerCount },
                { transition.aspectMask, transition.baseMipLevel + 1, transition.baseArrayLayer, transition.layerCount },
                FILTER_TYPE_LINEAR);

            // 将生成后的层级设置到src
            TextureBarrier(commandBuffer, 
            {src, 
            RESOURCE_STATE_TRANSFER_DST, RESOURCE_STATE_TRANSFER_SRC, 
                    {TEXTURE_ASPECT_COLOR, transition.baseMipLevel + 1, 1, transition.baseArrayLayer, transition.layerCount}});

            transition.baseMipLevel++;
        }
    }
}

VulkanRHICommandContext::VulkanRHICommandContext(RHICommandPoolRef pool, const VulkanRHIBackend& backend)
: RHICommandContext(pool)
{
    this->pool = ResourceCast(pool.get());

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = this->pool->GetHandle();
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(backend.GetLogicalDevice(), &allocInfo, &handle) != VK_SUCCESS) 
    {
        LOG_FATAL("Failed to allocate command buffer!");
    }
}

void VulkanRHICommandContext::Destroy() 
{
    vkFreeCommandBuffers(Backend()->GetLogicalDevice(), pool->GetHandle(), 1, &handle);
}

void VulkanRHICommandContext::BeginCommand()
{

    vkResetCommandBuffer(handle, 0);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;

    vkBeginCommandBuffer(handle, &beginInfo);
}

void VulkanRHICommandContext::EndCommand()
{
    if (vkEndCommandBuffer(handle) != VK_SUCCESS) 
    {
        LOG_FATAL("Failed to end command buffer!");
    }
}

void VulkanRHICommandContext::Submit(const RHIQueueSubmitBatch& batch, const std::vector<RHICommandContextRef>& contexts)
{
    std::shared_ptr<VulkanRHIQueue> queue = ResourceCast(batch.queue);

    // 收集命令缓冲（contexts须全部来自目标队列族的pool；空contexts=空提交，仅做信号量中继/帧尾收口）
    std::vector<VkCommandBufferSubmitInfo> commandBufferInfos;
    commandBufferInfos.reserve(contexts.size());
    for (const RHICommandContextRef& context : contexts)
    {
        VkCommandBufferSubmitInfo commandInfo{};
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandInfo.commandBuffer = static_cast<VulkanRHICommandContext*>(context.get())->GetHandle();
        commandBufferInfos.push_back(commandInfo);
    }

    // wait stage由waitState推导，再按目标队列族能力裁剪（compute族不能带图形专属stage）
    const VkQueueFlags queueFlags = Backend()->GetQueueFamilyProperties()[queue->GetQueueFamilyIndex()].queueFlags;
    std::vector<VkSemaphoreSubmitInfo> waitInfos;
    waitInfos.reserve(batch.waits.size());
    for (const RHISemaphoreWaitInfo& wait : batch.waits)
    {
        if (wait.semaphore == nullptr) continue;

        VkPipelineStageFlags2 stage = 0;
        if (wait.isTimeline)
        {
            // 同步点/跨帧链/join的timeline wait=批次级同步：掩码必须覆盖本批【一切】可能的访问阶段。
            // 单一资源to_state推导会漏阶段（sync validation实证：SSIS合并点只带一个资源的态，
            // Forward的深度附件读在EARLY_FRAGMENT_TESTS、被COLOR-only掩码漏掉→READ_AFTER_WRITE）。
            // 批次本来就整批等待，ALL_COMMANDS无性能损失
            stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        }
        else if (wait.waitState == RESOURCE_STATE_UNDEFINED)
        {
            // UNDEFINED=宽掩码约定：兼容旧Execute/ExecuteBatch的固定stage（如swapchain acquire）；
            // 手动按队列能力门控——COLOR_ATTACHMENT_OUTPUT仅图形族有效，compute/transfer任何族都有效
            stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            if (queueFlags & VK_QUEUE_GRAPHICS_BIT)
                stage |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else
        {
            stage = VulkanUtil::AccessFlagsToPipelineStageFlags(
                VulkanUtil::ResourceStateToAccessFlags(wait.waitState, queueFlags), queueFlags);
        }

        VkSemaphoreSubmitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitInfo.semaphore = ResourceCast(wait.semaphore)->GetHandle();
        waitInfo.stageMask = stage;
        waitInfo.value = wait.value;        // binary信号量忽略value
        waitInfos.push_back(waitInfo);
    }

    std::vector<VkSemaphoreSubmitInfo> signalInfos;
    signalInfos.reserve(batch.signals.size());
    for (const RHISemaphoreSubmitInfo& signal : batch.signals)
    {
        if (signal.semaphore == nullptr) continue;

        VkSemaphoreSubmitInfo signalInfo{};
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfo.semaphore = ResourceCast(signal.semaphore)->GetHandle();
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalInfo.value = signal.value;
        signalInfos.push_back(signalInfo);
    }

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = static_cast<uint32_t>(commandBufferInfos.size());
    submitInfo.pCommandBufferInfos = commandBufferInfos.data();
    submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
    submitInfo.pWaitSemaphoreInfos = waitInfos.data();
    submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size());
    submitInfo.pSignalSemaphoreInfos = signalInfos.data();

    VkFence signalFence = batch.signalFence != nullptr ? ResourceCast(batch.signalFence)->GetHandle() : VK_NULL_HANDLE;


    if (vkQueueSubmit2(queue->GetHandle(), 1, &submitInfo, signalFence) != VK_SUCCESS)
    {
        LOG_FATAL("Failed to submit queue batch!");
    }
}

void VulkanRHICommandContext::TextureBarrier(const RHITextureBarrier& barrier)
{
    ::TextureBarrier(handle, barrier, ResourceCast(pool->GetQueue())->GetFamilyIndex());
}

void VulkanRHICommandContext::BufferBarrier(const RHIBufferBarrier& barrier)
{
    ::BufferBarrier(handle, barrier, ResourceCast(pool->GetQueue())->GetFamilyIndex());
}

void VulkanRHICommandContext::CopyTextureToBuffer(RHITextureRef src, TextureSubresourceLayers srcSubresource, RHIBufferRef dst, uint64_t dstOffset)
{
    ::CopyTextureToBuffer(handle, src, srcSubresource, dst, dstOffset);
}

void VulkanRHICommandContext::CopyBufferToTexture(RHIBufferRef src, uint64_t srcOffset, RHITextureRef dst, TextureSubresourceLayers dstSubresource)
{
    ::CopyBufferToTexture(handle, src, srcOffset, dst, dstSubresource);
}

void VulkanRHICommandContext::CopyBuffer(RHIBufferRef src, uint64_t srcOffset, RHIBufferRef dst, uint64_t dstOffset, uint64_t size)
{
    ::CopyBuffer(handle, src, srcOffset, dst, dstOffset, size);
}

void VulkanRHICommandContext::CopyTexture(RHITextureRef src, TextureSubresourceLayers srcSubresource, RHITextureRef dst, TextureSubresourceLayers dstSubresource)
{
    ::CopyTexture(handle, src, srcSubresource, dst, dstSubresource);
}

void VulkanRHICommandContext::GenerateMips(RHITextureRef src)
{
    ::GenerateMips(handle, src);
}

void VulkanRHICommandContext::BuildTopLevelAccelerationStructure(RHITopLevelAccelerationStructureRef tlas)
{
    ResourceCast(tlas)->RecordBuild(handle);
} 

void VulkanRHICommandContext::PushEvent(const std::string& name, Color3 color) 
{
    VkDebugUtilsLabelEXT label_info;
    label_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label_info.pNext = nullptr;
    label_info.pLabelName = name.c_str();
    label_info.color[0] = color.r;
    label_info.color[1] = color.g;
    label_info.color[2] = color.b;
    label_info.color[3] = 1.0f;

    vkCmdBeginDebugUtilsLabelEXT(handle, &label_info);
}   

void VulkanRHICommandContext::PopEvent() 
{
    vkCmdEndDebugUtilsLabelEXT(handle);
}

void VulkanRHICommandContext::BeginRendering(const RHIRenderingInfo& rendering)
{
	std::vector<VkRenderingAttachmentInfo> colorAttachments;
    for (const AttachmentInfo& attachment : rendering.colorAttachments)
    {
        if (attachment.textureView == nullptr) break;

		VkRenderingAttachmentInfo colorAttachment = {};
		colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		colorAttachment.imageView = ResourceCast(attachment.textureView)->GetHandle();
		colorAttachment.imageLayout = VulkanUtil::ResourceStateToImageLayout(attachment.currentState);
		colorAttachment.loadOp = VulkanUtil::AttachmentLoadOpToVk(attachment.loadOp);
		colorAttachment.storeOp = VulkanUtil::AttachmentStoreOpToVk(attachment.storeOp);
		colorAttachment.clearValue.color.float32[0] = attachment.clearColor.r;
		colorAttachment.clearValue.color.float32[1] = attachment.clearColor.g;
		colorAttachment.clearValue.color.float32[2] = attachment.clearColor.b;
		colorAttachment.clearValue.color.float32[3] = attachment.clearColor.a;

        if (attachment.resolveMode != ResolveMode::RESOLVE_MODE_NONE)
        {
            assert(attachment.resolveTextureView != nullptr);
            colorAttachment.resolveImageView = ResourceCast(attachment.resolveTextureView)->GetHandle();
            colorAttachment.resolveImageLayout = VulkanUtil::ResourceStateToImageLayout(attachment.resolveCurrentState);
            colorAttachment.resolveMode = VulkanUtil::ResolveModeToVk(attachment.resolveMode);
        }

		colorAttachments.push_back(colorAttachment);
    }

	//// 第一个是depth，第二个是stencil
 //   VkRenderingAttachmentInfo depthStencilAttachment[2] = {};

 //   if (rendering.depthStencilAttachment.textureView != nullptr)
 //   {
 //       depthStencilAttachment[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
 //       depthStencilAttachment[0].imageView = ResourceCast(rendering.depthStencilAttachment.textureView)->GetHandle();
 //       depthStencilAttachment[0].imageLayout = VulkanUtil::ResourceStateToImageLayout(rendering.depthStencilAttachment.currentState);
 //       depthStencilAttachment[0].loadOp = VulkanUtil::AttachmentLoadOpToVk(rendering.depthStencilAttachment.loadOp);
 //       depthStencilAttachment[0].storeOp = VulkanUtil::AttachmentStoreOpToVk(rendering.depthStencilAttachment.storeOp);
 //       depthStencilAttachment[0].clearValue.depthStencil.depth = rendering.depthStencilAttachment.clearDepth;
 //       if (rendering.depthStencilAttachment.resolveMode != ResolveMode::RESOLVE_MODE_NONE)
 //       {
 //           assert(rendering.depthStencilAttachment.resolveTextureView != nullptr);
 //           depthStencilAttachment[0].resolveImageView = ResourceCast(rendering.depthStencilAttachment.resolveTextureView)->GetHandle();
 //           depthStencilAttachment[0].resolveImageLayout = VulkanUtil::ResourceStateToImageLayout(rendering.depthStencilAttachment.resolveCurrentState);
 //           depthStencilAttachment[0].resolveMode = VulkanUtil::ResolveModeToVk(rendering.depthStencilAttachment.resolveMode);
 //       }

 //       depthStencilAttachment[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	//	depthStencilAttachment[1].imageView = ResourceCast(rendering.depthStencilAttachment.textureView)->GetHandle();
	//	depthStencilAttachment[1].imageLayout = VulkanUtil::ResourceStateToImageLayout(rendering.depthStencilAttachment.currentState);
	//	depthStencilAttachment[1].loadOp = VulkanUtil::AttachmentLoadOpToVk(rendering.depthStencilAttachment.loadOp);
	//	depthStencilAttachment[1].storeOp = VulkanUtil::AttachmentStoreOpToVk(rendering.depthStencilAttachment.storeOp);    
	//	depthStencilAttachment[1].clearValue.depthStencil.stencil = rendering.depthStencilAttachment.clearStencil;
 //       if (rendering.depthStencilAttachment.resolveMode != ResolveMode::RESOLVE_MODE_NONE)
 //       {
 //           assert(rendering.depthStencilAttachment.resolveTextureView != nullptr);
 //           depthStencilAttachment[1].resolveImageView = ResourceCast(rendering.depthStencilAttachment.resolveTextureView)->GetHandle();
 //           depthStencilAttachment[1].resolveImageLayout = VulkanUtil::ResourceStateToImageLayout(rendering.depthStencilAttachment.resolveCurrentState);
 //           depthStencilAttachment[1].resolveMode = VulkanUtil::ResolveModeToVk(rendering.depthStencilAttachment.resolveMode);
 //       }
 //   };

	VkRenderingAttachmentInfo depthStencilAttachment = {};

	if (rendering.depthStencilAttachment.textureView != nullptr)
	{
		depthStencilAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depthStencilAttachment.imageView = ResourceCast(rendering.depthStencilAttachment.textureView)->GetHandle();
		depthStencilAttachment.imageLayout = VulkanUtil::ResourceStateToImageLayout(rendering.depthStencilAttachment.currentState);
		depthStencilAttachment.loadOp = VulkanUtil::AttachmentLoadOpToVk(rendering.depthStencilAttachment.loadOp);
		depthStencilAttachment.storeOp = VulkanUtil::AttachmentStoreOpToVk(rendering.depthStencilAttachment.storeOp);
		depthStencilAttachment.clearValue.depthStencil.depth = rendering.depthStencilAttachment.clearDepth;
		depthStencilAttachment.clearValue.depthStencil.stencil = rendering.depthStencilAttachment.clearStencil;
		if (rendering.depthStencilAttachment.resolveMode != ResolveMode::RESOLVE_MODE_NONE)
		{
			assert(rendering.depthStencilAttachment.resolveTextureView != nullptr);
			depthStencilAttachment.resolveImageView = ResourceCast(rendering.depthStencilAttachment.resolveTextureView)->GetHandle();
			depthStencilAttachment.resolveImageLayout = VulkanUtil::ResourceStateToImageLayout(rendering.depthStencilAttachment.resolveCurrentState);
			depthStencilAttachment.resolveMode = VulkanUtil::ResolveModeToVk(rendering.depthStencilAttachment.resolveMode);
		}
	}

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;

    // 颜色附件
	renderingInfo.pColorAttachments = colorAttachments.data();
	renderingInfo.colorAttachmentCount = (uint32_t)colorAttachments.size();


    // 深度模板附件
	if (rendering.depthStencilAttachment.textureView != nullptr)
	{
        RHIFormat format = rendering.depthStencilAttachment.textureView->GetInfo().format;

        if (IsDepthFormat(format))
            renderingInfo.pDepthAttachment = &depthStencilAttachment;
        if (IsStencilFormat(format))
            renderingInfo.pStencilAttachment = &depthStencilAttachment;
	}

	// 渲染区域
	renderingInfo.renderArea.offset = { (int32_t)rendering.offset.x, (int32_t)rendering.offset.y };
	renderingInfo.renderArea.extent = VulkanUtil::ExtentToVk(rendering.extent);

    // 图层数量
	renderingInfo.layerCount = rendering.layers;

	// 渲染标志
    renderingInfo.flags = VulkanUtil::RenderingFlagsToVk(rendering.renderingFlags);

	// 多视图渲染
	renderingInfo.viewMask = VulkanUtil::MultiviewCountToBitmask(rendering.multiviewCount);

    // 注意动态渲染中Context将无法保留renderPass，所以renderPass相关的缓存信息都需要另外保存
    // 用于实现指令的优化
	vkCmdBeginRendering(handle, &renderingInfo);
}

void VulkanRHICommandContext::EndRendering()
{
    vkCmdEndRendering(handle);
}

void VulkanRHICommandContext::SetViewport(Offset2D min, Offset2D max) 
{
    VkViewport viewport{};
    viewport.x = (float)min.x;
    viewport.y = (float)min.y;
    viewport.width = (float)(max.x - min.x);
    viewport.height = (float)(max.y - min.y);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(handle, 0, 1, &viewport);
}

void VulkanRHICommandContext::SetScissor(Offset2D min, Offset2D max) 
{
    VkRect2D scissor{};
    scissor.offset = {(int32_t)min.x, (int32_t)min.y};
    scissor.extent = {(uint32_t)(max.x - min.x), (uint32_t)(max.y - min.y)};
    vkCmdSetScissor(handle, 0, 1, &scissor);
}

void VulkanRHICommandContext::ClearScissors(const std::vector<ClearAttachment>& attachments, const std::vector<Rect2D>& scissors, uint32_t baseArrayLayer, uint32_t layerCount)
{
    std::vector<VkClearAttachment> clearAttachments;
    for(auto& attachment : attachments)
    {
        VkClearAttachment clearAttachment = {};
        clearAttachment.colorAttachment = attachment.binding;
        clearAttachment.clearValue.color.float32[0] = attachment.clearColor.r;
        clearAttachment.clearValue.color.float32[1] = attachment.clearColor.g;
        clearAttachment.clearValue.color.float32[2] = attachment.clearColor.b;
        clearAttachment.clearValue.color.float32[3] = attachment.clearColor.a;
        clearAttachment.aspectMask = VulkanUtil::TextureAspectToVk(attachment.aspect);
        clearAttachments.emplace_back(clearAttachment);
    }
    std::vector<VkClearRect> clearRects;
    for(auto& scissor : scissors)
    {
        if(scissor.extent.width == 0 || scissor.extent.height == 0)
            continue;

        VkClearRect rect = {};
        rect.baseArrayLayer = baseArrayLayer;
        rect.layerCount = layerCount;
        rect.rect.offset = VkOffset2D(scissor.offset.x, scissor.offset.y);
        rect.rect.extent = VkExtent2D(scissor.extent.width, scissor.extent.height);
        clearRects.emplace_back(rect);
    }
    if(clearRects.size() > 0)
        vkCmdClearAttachments(handle, 
        clearAttachments.size(), clearAttachments.data(), 
        clearRects.size(), clearRects.data());
}

void VulkanRHICommandContext::SetDepthBias(float constantBias, float slopeBias, float clampBias)
{
    vkCmdSetDepthBias(handle, constantBias, clampBias, slopeBias);
}

void VulkanRHICommandContext::SetLineWidth(float width)
{
    vkCmdSetLineWidth(handle, width);
}

void VulkanRHICommandContext::SetGraphicsPipeline(RHIGraphicsPipelineRef graphicsPipeline) 
{
    this->graphicsPipeline = ResourceCast(graphicsPipeline).get();
    this->computePipeline = nullptr;
    this->rayTraycingPipeline =  nullptr;

    this->graphicsPipeline->Bind(handle);
}

void VulkanRHICommandContext::SetComputePipeline(RHIComputePipelineRef computePipeline) 
{
    this->graphicsPipeline = nullptr;
    this->computePipeline = ResourceCast(computePipeline).get();
    this->rayTraycingPipeline = nullptr;

    this->computePipeline->Bind(handle);
}	

void VulkanRHICommandContext::SetRayTracingPipeline(RHIRayTracingPipelineRef rayTracingPipeline)
{
    this->graphicsPipeline = nullptr;
    this->computePipeline = nullptr;
    this->rayTraycingPipeline = ResourceCast(rayTracingPipeline).get();

    this->rayTraycingPipeline->Bind(handle);
}

void VulkanRHICommandContext::PushConstants(void* data, uint16_t size, ShaderFrequency frequency) 
{
    vkCmdPushConstants(handle, 
        GetCuttentPipelineLayout(), 
        VulkanUtil::ShaderFrequencyToVkStageFlags(frequency), 
        0, size, data);
}

void VulkanRHICommandContext::BindDescriptorSet(RHIDescriptorSetRef descriptor, uint32_t set)
{
    vkCmdBindDescriptorSets(handle, 
        GetCuttentBindingPoint(), 
        GetCuttentPipelineLayout(), 
        set, 1, &ResourceCast(descriptor)->GetHandle(), 
        0,              //TODO dynamic offset
        nullptr);
}

void VulkanRHICommandContext::BindVertexBuffer(RHIBufferRef vertexBuffer, uint32_t streamIndex, uint32_t offset)
{
    VkDeviceSize offsets = offset;
    vkCmdBindVertexBuffers(handle, streamIndex, 1, &ResourceCast(vertexBuffer)->GetHandle(), &offsets);
}

void VulkanRHICommandContext::BindIndexBuffer(RHIBufferRef indexBuffer, uint32_t offset)
{
    vkCmdBindIndexBuffer(handle, ResourceCast(indexBuffer)->GetHandle(), offset, VK_INDEX_TYPE_UINT32);    // 固定了索引用32位的
}

void VulkanRHICommandContext::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) 
{
    vkCmdDispatch(handle, groupCountX, groupCountY, groupCountZ);
}

void VulkanRHICommandContext::DispatchIndirect(RHIBufferRef argumentBuffer, uint32_t argumentOffset) 
{
    LOG_FATAL("VulkanRHICommandContext::DispatchIndirect is not implemented yet!");
}

void VulkanRHICommandContext::TraceRays(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    assert(this->rayTraycingPipeline != nullptr);

    //gl_LaunchSizeEXT 对应此处给出的3维尺寸
    //gl_LaunchIDEXT 类似于compute shader的gl_GlobalInvocationID，对应调用shader的坐标(ID)
    vkCmdTraceRaysKHR(
        handle,
        &this->rayTraycingPipeline->GetRaygenRegion(),
        &this->rayTraycingPipeline->GetRayMissRegion(),
        &this->rayTraycingPipeline->GetHitRegion(),
        &this->rayTraycingPipeline->GetCallableRegion(),
        groupCountX,
        groupCountY,
        groupCountZ);
}

void VulkanRHICommandContext::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) 
{
    vkCmdDraw(handle, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanRHICommandContext::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance) 
{
    vkCmdDrawIndexed(handle, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanRHICommandContext::DrawIndirect(RHIBufferRef argumentBuffer, uint32_t offset, uint32_t drawCount) 
{
    vkCmdDrawIndirect(handle, ResourceCast(argumentBuffer)->GetHandle(), offset, drawCount, sizeof(RHIIndirectCommand));
}

void VulkanRHICommandContext::DrawIndexedIndirect(RHIBufferRef argumentBuffer, uint32_t offset, uint32_t drawCount)
{
    vkCmdDrawIndexedIndirect(handle, ResourceCast(argumentBuffer)->GetHandle(), offset, drawCount, sizeof(RHIIndexedIndirectCommand));
}

void VulkanRHICommandContext::ImGuiCreateFontsTexture()
{
    // ImGui_ImplVulkan_CreateFontsTexture(handle);
}

void VulkanRHICommandContext::ImGuiRenderDrawData(ImGuiDrawFunc func)
{
    ImGui_ImplVulkan_NewFrame();
    // ImGui_ImplGlfw_NewFrame();
	ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    IMGUIZMO_NAMESPACE::BeginFrame();

    func();

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), handle);
}

VkPipelineLayout VulkanRHICommandContext::GetCuttentPipelineLayout()
{
    if (graphicsPipeline != nullptr)    return graphicsPipeline->GetPipelineLayout();
    if (computePipeline != nullptr)     return computePipeline->GetPipelineLayout();
    if (rayTraycingPipeline != nullptr) return rayTraycingPipeline->GetPipelineLayout();

    LOG_FATAL("Havent bind any pipeline!"); return nullptr;
}

VkPipelineBindPoint VulkanRHICommandContext::GetCuttentBindingPoint()
{
    if (graphicsPipeline != nullptr)        return VK_PIPELINE_BIND_POINT_GRAPHICS;
    if (computePipeline != nullptr)         return VK_PIPELINE_BIND_POINT_COMPUTE;
    if (rayTraycingPipeline != nullptr)     return VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;

    LOG_FATAL("Havent bind any pipeline!"); return VK_PIPELINE_BIND_POINT_MAX_ENUM;
}


VulkanRHICommandContextImmediate::VulkanRHICommandContextImmediate(VulkanRHIBackend& backend)
: RHICommandContextImmediate()
{
    fence = backend.CreateFence(true);
    queue = backend.GetQueue({QUEUE_TYPE_GRAPHICS, 0}); // 用QUEUE_TYPE_TRANSFER其实就行？
    commandPool = backend.CreateCommandPool({queue});
    device = backend.GetLogicalDevice();

    BeginSingleTimeCommand();
}

void VulkanRHICommandContextImmediate::Flush()
{
    EndSingleTimeCommand();
    BeginSingleTimeCommand();
}

void VulkanRHICommandContextImmediate::TextureBarrier(const RHITextureBarrier& barrier)
{
    ::TextureBarrier(handle, barrier);
}

void VulkanRHICommandContextImmediate::BufferBarrier(const RHIBufferBarrier& barrier)
{

    ::BufferBarrier(handle, barrier);
}

void VulkanRHICommandContextImmediate::CopyTextureToBuffer(RHITextureRef src, TextureSubresourceLayers srcSubresource, RHIBufferRef dst, uint64_t dstOffset)
{
    ::CopyTextureToBuffer(handle, src, srcSubresource, dst, dstOffset);
}

void VulkanRHICommandContextImmediate::CopyBufferToTexture(RHIBufferRef src, uint64_t srcOffset, RHITextureRef dst, TextureSubresourceLayers dstSubresource)
{
    ::CopyBufferToTexture(handle, src, srcOffset, dst, dstSubresource);
}

void VulkanRHICommandContextImmediate::CopyBuffer(RHIBufferRef src, uint64_t srcOffset, RHIBufferRef dst, uint64_t dstOffset, uint64_t size)
{
    ::CopyBuffer(handle, src, srcOffset, dst, dstOffset, size);
}

void VulkanRHICommandContextImmediate::CopyTexture(RHITextureRef src, TextureSubresourceLayers srcSubresource, RHITextureRef dst, TextureSubresourceLayers dstSubresource)
{
    ::CopyTexture(handle, src, srcSubresource, dst, dstSubresource);
}

void VulkanRHICommandContextImmediate::GenerateMips(RHITextureRef src)
{
    ::GenerateMips(handle, src);
}

void VulkanRHICommandContextImmediate::BeginSingleTimeCommand()
{
    // 重新分配VkCommandBuffer
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = ResourceCast(commandPool)->GetHandle();
    allocInfo.commandBufferCount = 1;

    vkAllocateCommandBuffers(device, &allocInfo, &handle);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(handle, &beginInfo);
}

void VulkanRHICommandContextImmediate::EndSingleTimeCommand()
{
    // 等待前一次flush执行完成
    fence->Wait();
    if(oldHandle != VK_NULL_HANDLE) vkFreeCommandBuffers(Backend()->GetLogicalDevice(), ResourceCast(commandPool)->GetHandle(), 1, &oldHandle);

    // 提交最新一次的flush
    if (vkEndCommandBuffer(handle) != VK_SUCCESS) {
        LOG_FATAL("Failed to record command buffer!");
    }

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &handle;
    submitInfo.waitSemaphoreCount = 0;
    submitInfo.pWaitSemaphores = nullptr;
    submitInfo.pWaitDstStageMask = nullptr;
    submitInfo.signalSemaphoreCount = 0;
    submitInfo.pSignalSemaphores = nullptr;

    VkResult result = vkQueueSubmit(ResourceCast(queue)->GetHandle(), 1, &submitInfo, ResourceCast(fence)->GetHandle());
    if (result != VK_SUCCESS) 
    {
        LOG_FATAL("Failed to submit draw command buffer! [%d]", result);
    }

    oldHandle = handle;
}