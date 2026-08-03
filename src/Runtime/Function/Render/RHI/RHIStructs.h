#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#define MAX_QUEUE_CNT 2					//每个队列族的最大队列数目
#define MAX_RENDER_TARGETS 8			//允许同时绑定的最大RT数目
#define MAX_SHADER_IN_OUT_VARIABLES 8	//允许着色器最大的输入和输出变量数目
#define MAX_DESCRIPTOR_SETS 8			//允许绑定的最大描述符集数目

// CommandContext与命令缓冲区目前处于一对一的关系
using RHICommandContextImmediateRef = std::shared_ptr<class RHICommandContextImmediate> ;
using RHICommandContextRef = std::shared_ptr<class RHICommandContext> ;

// CommandList与CommandContext目前处于一对一的关系
using RHICommandListRef = std::shared_ptr<class RHICommandList>;
using RHICommandListImmediateRef = std::shared_ptr<class RHICommandListImmediate>;

using RHIBackendRef = std::shared_ptr<class RHIBackend> ;
using RHIResourceRef = std::shared_ptr<class RHIResource> ;
using RHIBufferRef = std::shared_ptr<class RHIBuffer> ;
using RHITextureRef = std::shared_ptr<class RHITexture> ;
using RHITextureViewRef = std::shared_ptr<class RHITextureView> ;
using RHISamplerRef = std::shared_ptr<class RHISampler> ;
using RHIShaderRef = std::shared_ptr<class RHIShader> ;
using RHIShaderBindingTableRef = std::shared_ptr<class RHIShaderBindingTable> ;
using RHITopLevelAccelerationStructureRef = std::shared_ptr<class RHITopLevelAccelerationStructure> ;
using RHIBottomLevelAccelerationStructureRef = std::shared_ptr<class RHIBottomLevelAccelerationStructure> ;
using RHIRootSignatureRef = std::shared_ptr<class RHIRootSignature> ;
using RHIDescriptorSetRef = std::shared_ptr<class RHIDescriptorSet> ;
// TODO: 准备放弃
using RHIRenderPassRef = std::shared_ptr<class RHIRenderPass> ;
using RHIGraphicsPipelineRef = std::shared_ptr<class RHIGraphicsPipeline> ;
using RHIComputePipelineRef = std::shared_ptr<class RHIComputePipeline> ;
using RHIRayTracingPipelineRef = std::shared_ptr<class RHIRayTracingPipeline> ;
using RHIQueueRef = std::shared_ptr<class RHIQueue> ;
using RHISurfaceRef = std::shared_ptr<class RHISurface> ;
using RHISwapchainRef = std::shared_ptr<class RHISwapchain> ;

// RHICommandContext 的对象池 + RHICommandList 的工厂
using RHICommandPoolRef = std::shared_ptr<class RHICommandPool> ;

using RHIFenceRef = std::shared_ptr<class RHIFence> ;

using RHISemaphoreRef = std::shared_ptr<class RHISemaphore> ;

enum RHIResourceType : uint32_t	// 此处的倒序也是有效的析构顺序
{
	RHI_BUFFER = 0,
	RHI_TEXTURE,
	RHI_TEXTURE_VIEW,
	RHI_SAMPLER,
	RHI_SHADER,
	RHI_SHADER_BINDING_TABLE,
	RHI_TOP_LEVEL_ACCELERATION_STRUCTURE,
	RHI_BOTTOM_LEVEL_ACCELERATION_STRUCTURE,

	RHI_ROOT_SIGNATURE,
	RHI_DESCRIPTOR_SET,
	
	RHI_RENDER_PASS,
	RHI_GRAPHICS_PIPELINE,
	RHI_COMPUTE_PIPELINE,
	RHI_RAY_TRACING_PIPELINE,

	RHI_QUEUE,
	RHI_SURFACE,
	RHI_SWAPCHAIN,
	RHI_COMMAND_POOL,
	RHI_COMMAND_CONTEXT,
	RHI_COMMAND_CONTEXT_IMMEDIATE,
	RHI_FENCE,
	RHI_SEMAPHORE,

	RHI_RESOURCE_TYPE_MAX_CNT,	//
};

enum QueueType : uint32_t
{
	QUEUE_TYPE_GRAPHICS = 0,
	QUEUE_TYPE_COMPUTE,
	QUEUE_TYPE_TRANSFER,

	QUEUE_TYPE_MAX_ENUM,	//
};

enum MemoryUsage : uint32_t
{
    MEMORY_USAGE_UNKNOWN = 0,
    MEMORY_USAGE_GPU_ONLY = 1,		// 仅GPU使用，在VRAM显存上分配，不可绑定
    MEMORY_USAGE_CPU_ONLY = 2,		// HOST_VISIBLE &&  HOST_COHERENT 及时同步，不需要flush到GPU，GPU可访问但是很慢
    MEMORY_USAGE_CPU_TO_GPU = 3,	// HOST_VISIBLE CPU端uncached，用于CPU端频繁进行数据写入，GPU端对数据进行读取
    MEMORY_USAGE_GPU_TO_CPU = 4,	// HOST_VISIBLE CPU端cached，用于被GPU写入且被CPU读取

    MEMORY_USAGE_MAX_ENUM = 0x7FFFFFFF,		//
};

enum ResourceTypeBits : uint32_t	//资源类型，封装了UsageFlag和DescriptorType，在底层实现做推断
{
	RESOURCE_TYPE_NONE = 0,
	RESOURCE_TYPE_SAMPLER = 1 << 0,
	RESOURCE_TYPE_TEXTURE = 1 << 1,
	RESOURCE_TYPE_RW_TEXTURE = 1 << 2,
	RESOURCE_TYPE_TEXTURE_CUBE = 1 << 3,
	RESOURCE_TYPE_RENDER_TARGET = 1 << 4,
	RESOURCE_TYPE_COMBINED_IMAGE_SAMPLER = 1 << 5,
	RESOURCE_TYPE_BUFFER = 1 << 6,
	RESOURCE_TYPE_RW_BUFFER = 1 << 7,
	RESOURCE_TYPE_UNIFORM_BUFFER = 1 << 8,
	RESOURCE_TYPE_VERTEX_BUFFER = 1 << 9,
	RESOURCE_TYPE_INDEX_BUFFER = 1 << 10,
	RESOURCE_TYPE_INDIRECT_BUFFER = 1 << 11,
	RESOURCE_TYPE_TEXEL_BUFFER = 1 << 12,		// 纹素缓冲区
	RESOURCE_TYPE_RW_TEXEL_BUFFER = 1 << 13,
	RESOURCE_TYPE_RAY_TRACING = 1 << 14,

	RESOURCE_TYPE_MAX_ENUM = 0x7FFFFFFF,
};
using ResourceType = uint32_t;

enum BufferCreationFlagBits : uint32_t
{
	BUFFER_CREATION_NONE = 0,
	BUFFER_CREATION_PERSISTENT_MAP = 1 << 0,
	BUFFER_CREATION_FORCE_ALIGNMENT = 1 << 1,	// 使用256字节的内存对齐

	BUFFER_CREATION_MAX_ENUM = 0x7FFFFFFF,	//
};
using BufferCreationFlags = uint32_t;

enum TextureCreationFlagBits : uint32_t
{
	TEXTURE_CREATION_NONE = 0,
	TEXTURE_CREATION_FORCE_2D = 1 << 0,
	TEXTURE_CREATION_FORCE_3D = 1 << 1,

	TEXTURE_CREATION_MAX_ENUM = 0x7FFFFFFF,	//
};
using TextureCreationFlags = uint32_t;

enum RHIResourceState : uint32_t	//根据状态来设置对应的barrier做转换以及同步，buffer也有		
{
    RESOURCE_STATE_UNDEFINED = 0,
	RESOURCE_STATE_COMMON,
	RESOURCE_STATE_TRANSFER_SRC,
    RESOURCE_STATE_TRANSFER_DST,
    RESOURCE_STATE_VERTEX_BUFFER,
    RESOURCE_STATE_INDEX_BUFFER,
    RESOURCE_STATE_COLOR_ATTACHMENT,
	RESOURCE_STATE_DEPTH_STENCIL_ATTACHMENT,
    RESOURCE_STATE_UNORDERED_ACCESS,
	RESOURCE_STATE_SHADER_RESOURCE,
    RESOURCE_STATE_INDIRECT_ARGUMENT,
    RESOURCE_STATE_PRESENT,
    RESOURCE_STATE_ACCELERATION_STRUCTURE,

    RESOURCE_STATE_MAX_ENUM,	//
};

enum RHIFormat : uint32_t
{
	FORMAT_UKNOWN = 0,

	FORMAT_R8_SRGB,
	FORMAT_R8G8_SRGB,
	FORMAT_R8G8B8_SRGB,
	FORMAT_R8G8B8A8_SRGB,
	FORMAT_B8G8R8A8_SRGB,

	FORMAT_R16_SFLOAT,
	FORMAT_R16G16_SFLOAT,
	FORMAT_R16G16B16_SFLOAT,
	FORMAT_R16G16B16A16_SFLOAT,
	FORMAT_R32_SFLOAT,
	FORMAT_R32G32_SFLOAT,
	FORMAT_R32G32B32_SFLOAT,
	FORMAT_R32G32B32A32_SFLOAT,

	FORMAT_R8_UNORM,
	FORMAT_R8G8_UNORM,
	FORMAT_R8G8B8_UNORM,
	FORMAT_R8G8B8A8_UNORM,
	FORMAT_R16_UNORM,
	FORMAT_R16G16_UNORM,
	FORMAT_R16G16B16_UNORM,
	FORMAT_R16G16B16A16_UNORM,

	FORMAT_R8_SNORM,
	FORMAT_R8G8_SNORM,
	FORMAT_R8G8B8_SNORM,
	FORMAT_R8G8B8A8_SNORM,
	FORMAT_R16_SNORM,
	FORMAT_R16G16_SNORM,
	FORMAT_R16G16B16_SNORM,
	FORMAT_R16G16B16A16_SNORM,

	FORMAT_R8_UINT,
	FORMAT_R8G8_UINT,
	FORMAT_R8G8B8_UINT,
	FORMAT_R8G8B8A8_UINT,
	FORMAT_R16_UINT,
	FORMAT_R16G16_UINT,
	FORMAT_R16G16B16_UINT,
	FORMAT_R16G16B16A16_UINT,
	FORMAT_R32_UINT,
	FORMAT_R32G32_UINT,
	FORMAT_R32G32B32_UINT,
	FORMAT_R32G32B32A32_UINT,

	FORMAT_R8_SINT,
	FORMAT_R8G8_SINT,
	FORMAT_R8G8B8_SINT,
	FORMAT_R8G8B8A8_SINT,
	FORMAT_R16_SINT,
	FORMAT_R16G16_SINT,
	FORMAT_R16G16B16_SINT,
	FORMAT_R16G16B16A16_SINT,
	FORMAT_R32_SINT,
	FORMAT_R32G32_SINT,
	FORMAT_R32G32B32_SINT,
	FORMAT_R32G32B32A32_SINT,

	FORMAT_D32_SFLOAT, 
	FORMAT_D32_SFLOAT_S8_UINT, 
	FORMAT_D24_UNORM_S8_UINT,

	FORMAT_A2R10G10B10_SNORM,
    FORMAT_A2R10G10B10_UNORM,
    FORMAT_A2R10G10B10_SINT,
    FORMAT_A2R10G10B10_UINT,
    FORMAT_B10G11R11_UFLOAT,
    FORMAT_E5B9G9R9_UFLOAT,

	FORMAT_MAX_ENUM, 	//
};

static uint32_t FormatChanelCounts(RHIFormat format)
{
	switch (format) {
	case FORMAT_R8_SRGB:
	case FORMAT_R16_SFLOAT:
	case FORMAT_R32_SFLOAT:
	case FORMAT_R8_UNORM:
	case FORMAT_R16_UNORM:
	case FORMAT_R8_SNORM:
	case FORMAT_R16_SNORM:
	case FORMAT_R8_UINT:
	case FORMAT_R16_UINT:
	case FORMAT_R32_UINT:
	case FORMAT_R8_SINT:
	case FORMAT_R16_SINT:
	case FORMAT_R32_SINT:
	case FORMAT_D32_SFLOAT:
		return 1;

	case FORMAT_R8G8_SRGB:
	case FORMAT_R16G16_SFLOAT:
	case FORMAT_R32G32_SFLOAT:
	case FORMAT_R8G8_UNORM:
	case FORMAT_R16G16_UNORM:
	case FORMAT_R8G8_SNORM: 
	case FORMAT_R16G16_SNORM:
	case FORMAT_R8G8_UINT:
	case FORMAT_R16G16_UINT:
	case FORMAT_R32G32_UINT:
	case FORMAT_R8G8_SINT:
	case FORMAT_R16G16_SINT:
	case FORMAT_R32G32_SINT:
	case FORMAT_D32_SFLOAT_S8_UINT:
	case FORMAT_D24_UNORM_S8_UINT:
		return 2;

	case FORMAT_R8G8B8_SRGB:
	case FORMAT_R16G16B16_SFLOAT:
	case FORMAT_R32G32B32_SFLOAT:
	case FORMAT_R8G8B8_UNORM:
	case FORMAT_R16G16B16_UNORM:
	case FORMAT_R8G8B8_SNORM:
	case FORMAT_R16G16B16_SNORM:
	case FORMAT_R8G8B8_UINT:
	case FORMAT_R16G16B16_UINT:
	case FORMAT_R32G32B32_UINT:
	case FORMAT_R8G8B8_SINT:
	case FORMAT_R16G16B16_SINT:
	case FORMAT_R32G32B32_SINT:
	case FORMAT_B10G11R11_UFLOAT:
    case FORMAT_E5B9G9R9_UFLOAT:
		return 3;

	case FORMAT_R8G8B8A8_SRGB:
	case FORMAT_B8G8R8A8_SRGB:
	case FORMAT_R16G16B16A16_SFLOAT:     
	case FORMAT_R32G32B32A32_SFLOAT:     
	case FORMAT_R8G8B8A8_UNORM:  
	case FORMAT_R16G16B16A16_UNORM:  
	case FORMAT_R8G8B8A8_SNORM:  
	case FORMAT_R16G16B16A16_SNORM:  
	case FORMAT_R8G8B8A8_UINT:    
	case FORMAT_R16G16B16A16_UINT:
	case FORMAT_R32G32B32A32_UINT:
	case FORMAT_R8G8B8A8_SINT:   
	case FORMAT_R16G16B16A16_SINT:
	case FORMAT_R32G32B32A32_SINT:
	case FORMAT_A2R10G10B10_SNORM:
    case FORMAT_A2R10G10B10_UNORM:
    case FORMAT_A2R10G10B10_SINT:
    case FORMAT_A2R10G10B10_UINT:
		return 4;

	default:  
		return 0;
    }
}

static bool IsDepthStencilFormat(RHIFormat format)
{
	switch (format) {
	case FORMAT_D32_SFLOAT_S8_UINT:
	case FORMAT_D24_UNORM_S8_UINT:
		return true;
	default:
		return false;
	}
}

static bool IsDepthFormat(RHIFormat format)
{
	switch (format) {
	case FORMAT_D32_SFLOAT:
	case FORMAT_D32_SFLOAT_S8_UINT:
	case FORMAT_D24_UNORM_S8_UINT:
		return true;
	default:
		return false;
	}
}

static bool IsStencilFormat(RHIFormat format)
{
	switch (format) {
	case FORMAT_D32_SFLOAT_S8_UINT:
	case FORMAT_D24_UNORM_S8_UINT:
		return true;
	default:
		return false;
	}
}

static bool IsColorFormat(RHIFormat format)
{
	return !IsDepthFormat(format) && !IsStencilFormat(format);
}

static bool IsRWFormat(RHIFormat format)
{
	switch (format) {
	case FORMAT_D32_SFLOAT:
	case FORMAT_D32_SFLOAT_S8_UINT:
	case FORMAT_D24_UNORM_S8_UINT:
	case FORMAT_R8_SRGB:
	case FORMAT_R8G8_SRGB:
	case FORMAT_R8G8B8_SRGB:
	case FORMAT_R8G8B8A8_SRGB:
	case FORMAT_B8G8R8A8_SRGB:
		return false;
	default:
		return true;
	}
}

enum FilterType  : uint32_t
{
    FILTER_TYPE_NEAREST = 0,
    FILTER_TYPE_LINEAR,

    FILTER_TYPE_MAX_ENUM,	//
};

enum MipMapMode : uint32_t
{
    MIPMAP_MODE_NEAREST = 0,
    MIPMAP_MODE_LINEAR,

    MIPMAP_MODE_MAX_ENUM_BIT,	//
};

enum AddressMode : uint32_t
{
    ADDRESS_MODE_MIRROR,
    ADDRESS_MODE_REPEAT,
    ADDRESS_MODE_CLAMP_TO_EDGE,
    ADDRESS_MODE_CLAMP_TO_BORDER,

    ADDRESS_MODE_MAX_ENUM,	//
};

enum TextureViewType : uint32_t
{
	VIEW_TYPE_UNDEFINED = 0,
	VIEW_TYPE_1D,
    VIEW_TYPE_2D,
    VIEW_TYPE_3D,
    VIEW_TYPE_CUBE,
    VIEW_TYPE_1D_ARRAY,
    VIEW_TYPE_2D_ARRAY,
    VIEW_TYPE_CUBE_ARRAY,

    VIEW_TYPE_MAX_ENUM,		//	
};

enum TextureAspectFlagBits : uint32_t
{
	TEXTURE_ASPECT_NONE = 0,
	TEXTURE_ASPECT_COLOR = 1 << 0,
	TEXTURE_ASPECT_DEPTH = 1 << 1,
	TEXTURE_ASPECT_STENCIL = 1 << 2,

	TEXTURE_ASPECT_DEPTH_STENCIL = TEXTURE_ASPECT_DEPTH | TEXTURE_ASPECT_STENCIL,

	TEXTURE_ASPECT_MAX_ENUM = 0x7FFFFFFF,	//
};
using TextureAspectFlags = uint32_t;

enum ShaderFrequencyBits : uint32_t
{

	SHADER_FREQUENCY_COMPUTE = 1 << 0,
	SHADER_FREQUENCY_VERTEX = 1 << 1,
	SHADER_FREQUENCY_FRAGMENT = 1 << 2,
	SHADER_FREQUENCY_GEOMETRY = 1 << 3,
	SHADER_FREQUENCY_RAY_GEN = 1 << 4,
	SHADER_FREQUENCY_CLOSEST_HIT = 1 << 5,
	SHADER_FREQUENCY_RAY_MISS = 1 << 6,
	SHADER_FREQUENCY_INTERSECTION = 1 << 7,
	SHADER_FREQUENCY_ANY_HIT = 1 << 8,
	SHADER_FREQUENCY_MESH = 1 << 9,

	SHADER_FREQUENCY_GRAPHICS = 	SHADER_FREQUENCY_VERTEX | 
									SHADER_FREQUENCY_FRAGMENT | 
									SHADER_FREQUENCY_GEOMETRY | 
									SHADER_FREQUENCY_MESH,	
	SHADER_FREQUENCY_RAY_TRACING = 	SHADER_FREQUENCY_RAY_GEN | 
									SHADER_FREQUENCY_CLOSEST_HIT | 
									SHADER_FREQUENCY_RAY_MISS | 
									SHADER_FREQUENCY_INTERSECTION | 
									SHADER_FREQUENCY_ANY_HIT,
	SHADER_FREQUENCY_ALL =			SHADER_FREQUENCY_GRAPHICS | 
									SHADER_FREQUENCY_COMPUTE | 	
									SHADER_FREQUENCY_RAY_TRACING,

	SHADER_FREQUENCY_MAX_ENUM = 0x7FFFFFFF, //
};
using ShaderFrequency = uint32_t;

enum AttachmentLoadOp : uint32_t
{
    ATTACHMENT_LOAD_OP_LOAD = 0,
    ATTACHMENT_LOAD_OP_CLEAR,
    ATTACHMENT_LOAD_OP_DONT_CARE,

    ATTACHMENT_LOAD_OP_MAX_ENUM,	//
};

enum AttachmentStoreOp : uint32_t
{
    ATTACHMENT_STORE_OP_STORE = 0,
    ATTACHMENT_STORE_OP_DONT_CARE = 1,

    ATTACHMENT_STORE_OP_MAX_ENUM, 	//
} ;

// 多重采样附件中多重采样值的解析模式
enum class ResolveMode :uint32_t
{
	RESOLVE_MODE_NONE = 0,
	RESOLVE_MODE_SAMPLE_ZERO,
	RESOLVE_MODE_AVERAGE,
	RESOLVE_MODE_MIN,
	RESOLVE_MODE_MAX,

	RESOLVE_MODE_MAX_ENUM,
};

// 将默认行为显式标出，避免设置无效位
enum RenderingFlagBits :uint32_t
{
	RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS = 0,
	RENDERING_SUSPENDING = 1 << 0,
	RENDERING_RESUMING = 1 << 1,

	RENDERING_MAX_ENUM = 0x7FFFFFFF,
};
using RenderingFlags = uint32_t;

// 图元类型
enum PrimitiveType : uint32_t
{
	PRIMITIVE_TYPE_TRIANGLE_LIST = 0,
	PRIMITIVE_TYPE_TRIANGLE_STRIP,
	PRIMITIVE_TYPE_LINE_LIST,
	PRIMITIVE_TYPE_POINT_LIST,	

	PRIMITIVE_TYPE_MAX_ENUM,	//
};

// 面填充模式
enum RasterizerFillMode : uint32_t
{
	FILL_MODE_POINT = 0,
	FILL_MODE_WIREFRAME,
	FILL_MODE_SOLID,

    FILL_MODE_MAX_ENUM,  //
};

// 面裁剪模式
enum RasterizerCullMode : uint32_t
{
	CULL_MODE_NONE = 0,
	CULL_MODE_FRONT,     
	CULL_MODE_BACK,

    CULL_MODE_MAX_ENUM,  //
};

// 深度裁剪 OR 深度钳制
enum RasterizerDepthClipMode : uint32_t
{
	DEPTH_CLIP = 0,
	DEPTH_CLAMP,

	DEPTH_CLIP_MODE_MAX_ENUM,    //
};

// 比较方式
enum CompareFunction : uint32_t
{
	COMPARE_FUNCTION_LESS = 0,
	COMPARE_FUNCTION_LESS_EQUAL,
	COMPARE_FUNCTION_GREATER,
	COMPARE_FUNCTION_GREATER_EQUAL,
	COMPARE_FUNCTION_EQUAL,
	COMPARE_FUNCTION_NOT_EQUAL,
	COMPARE_FUNCTION_NEVER,
	COMPARE_FUNCTION_ALWAYS,

	COMPARE_FUNCTION_MAX_ENUM,   //
};

// 采样缩减模式
enum SamplerReductionMode : uint32_t
{
	SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE = 0,	// 加权平均
	SAMPLER_REDUCTION_MODE_MIN,						// 取最小
	SAMPLER_REDUCTION_MODE_MAX,						// 取最大

	SAMPLER_REDUCTION_MODE_MAX_ENUM,   //
};

// 模板操作
enum StencilOp : uint32_t
{
	STENCIL_OP_KEEP = 0,				// 保持不变
	STENCIL_OP_ZERO,					// 置零
	STENCIL_OP_REPLACE,					// 替换成新值
	STENCIL_OP_SATURATED_INCREMENT,		// 饱和加1	
	STENCIL_OP_SATURATED_DECREMENT,		// 饱和减1
	STENCIL_OP_INVERT,					// 按位取反
	STENCIL_OP_INCREMENT,				// 加1(可能溢出回绕)
	STENCIL_OP_DECREMENT,				// 减1(可能下溢回绕)

    STENCIL_OP_MAX_ENUM, //
};

enum BlendOp : uint32_t
{
	BLEND_OP_ADD = 0,
	BLEND_OP_SUBTRACT,					// 减
	BLEND_OP_REVERSE_SUBTRACT,			// 反减
    BLEND_OP_MIN,
    BLEND_OP_MAX,

    BLEND_OP_MAX_ENUM, //
};

// 混合因子
enum BlendFactor : uint32_t
{
    BLEND_FACTOR_ZERO = 0,
    BLEND_FACTOR_ONE,
    BLEND_FACTOR_SRC_COLOR,
    BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    BLEND_FACTOR_DST_COLOR,
    BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    BLEND_FACTOR_SRC_ALPHA,
    BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    BLEND_FACTOR_DST_ALPHA,
    BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    BLEND_FACTOR_SRC_ALPHA_SATURATE,
    BLEND_FACTOR_CONSTANT_COLOR,
    BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,

    BLEND_FACTOR_MAX_ENUM, //
};

enum ColorWriteMaskBits : uint32_t
{
	COLOR_MASK_RED = 1 << 0,
	COLOR_MASK_GREEN = 1 << 1,
	COLOR_MASK_BLUE = 1 << 2,
	COLOR_MASK_ALPHA = 1 << 3,

	COLOR_MASK_NONE  = 0,
	COLOR_MASK_RGB   = COLOR_MASK_RED | COLOR_MASK_GREEN | COLOR_MASK_BLUE,
	COLOR_MASK_RGBA  = COLOR_MASK_RED | COLOR_MASK_GREEN | COLOR_MASK_BLUE | COLOR_MASK_ALPHA,
	COLOR_MASK_RG    = COLOR_MASK_RED | COLOR_MASK_GREEN,
	COLOR_MASK_BA    = COLOR_MASK_BLUE | COLOR_MASK_ALPHA,
};
using ColorWriteMasks = uint32_t;

// 索引间接绘制
struct RHIIndexedIndirectCommand 
{
    uint32_t    indexCount;
    uint32_t    instanceCount;
    uint32_t    firstIndex;
    int32_t     vertexOffset;
    uint32_t    firstInstance;
};

struct RHIIndirectCommand 
{
    uint32_t    vertexCount;
    uint32_t    instanceCount;
    uint32_t    firstVertex;
    uint32_t    firstInstance;
};

struct RHIAccelerationStructureInstanceInfo
{
	float    	transform[3][4] = { 0.0f };

    uint32_t    instanceIndex;
    uint32_t    mask;
    uint32_t    shaderBindingTableOffset;
    RHIBottomLevelAccelerationStructureRef    blas;

};

struct Extent2D 
{
    uint32_t    width = 0;
    uint32_t    height = 0;

	friend bool operator==(const Extent2D& a, const Extent2D& b)
	{
		return 	a.width == b.width &&
				a.height == b.height;
	}

	const uint32_t MipSize() const 
	{ 
		return (uint32_t)(std::floor(std::log2(std::max(width, height)))) + 1; 
	}

};

struct Extent3D 
{
    uint32_t    width = 0;
    uint32_t    height = 0;
    uint32_t    depth = 0;

	friend bool operator==(const Extent3D& a, const Extent3D& b)
	{
		return 	a.width == b.width &&
				a.height == b.height &&
				a.depth == b.depth;
	}
	
	const uint32_t MipSize() const 
	{ 
		return (uint32_t)(std::floor(std::log2(std::max(width, std::max(height, depth))))) + 1; 
	}

};

struct Offset2D 
{
    uint32_t    x = 0;
    uint32_t    y = 0;

	friend Offset2D operator+(const Offset2D& a, const Offset2D& b)
	{
		return {
			.x = a.x + b.x,
			.y = a.y + b.y
		};
	}

	friend bool operator==(const Offset2D& a, const Offset2D& b)
	{
		return 	a.x == b.x &&
				a.y == b.y;
	}

};

struct Offset3D 
{
    uint32_t    x = 0;
    uint32_t    y = 0;
    uint32_t    z = 0;

	friend Offset3D operator+(const Offset3D& a, const Offset3D& b)
	{
		return {
			.x = a.x + b.x,
			.y = a.y + b.y,
			.z = a.z + b.z
		};
	}

	friend bool operator==(const Offset3D& a, const Offset3D& b)
	{
		return 	a.x == b.x &&
				a.y == b.y &&
				a.z == b.z;
	}

};

struct Rect2D 
{
    Offset2D    offset = {};
    Extent2D    extent = {};
};

struct Color3 
{
    float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
};

struct Color4 
{
    float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	float a = 0.0f;
};

struct TextureSubresourceRange 
{
	TextureAspectFlags	  aspect = TEXTURE_ASPECT_NONE;
    uint32_t              baseMipLevel = 0;
    uint32_t              levelCount = 0;
    uint32_t              baseArrayLayer = 0;
    uint32_t              layerCount = 0;

	uint32_t			  __padding = 0;	

	friend bool operator==(const TextureSubresourceRange& a, const TextureSubresourceRange& b)
	{
		return 	a.aspect == b.aspect &&
				a.baseMipLevel == b.baseMipLevel &&
				a.levelCount == b.levelCount &&
				a.baseArrayLayer == b.baseArrayLayer &&
				a.layerCount == b.layerCount;
	}

	bool IsDefault() 
	{
		return 	aspect == TEXTURE_ASPECT_NONE &&
				baseMipLevel == 0 &&
				levelCount == 0 &&
				baseArrayLayer == 0 &&
				layerCount == 0;
	}

};

struct TextureSubresourceLayers
{
	TextureAspectFlags	  aspect = TEXTURE_ASPECT_NONE;
    uint32_t              mipLevel = 0;
    uint32_t              baseArrayLayer = 0;
    uint32_t              layerCount = 0;

	friend bool operator==(const TextureSubresourceLayers& a, const TextureSubresourceLayers& b)
	{
		return 	a.aspect == b.aspect &&
				a.mipLevel == b.mipLevel &&
				a.baseArrayLayer == b.baseArrayLayer &&
				a.layerCount == b.layerCount;
	}

	bool IsDefault() 
	{
		return 	aspect == TEXTURE_ASPECT_NONE &&
				mipLevel == 0 &&
				baseArrayLayer == 0 &&
				layerCount == 0;
	}

};

struct ClearAttachment
{
	uint32_t binding = 0;
	TextureAspectFlags aspect = TEXTURE_ASPECT_NONE;
	Color4 clearColor = {};
};

// 所在队列族的信息
struct RHIQueueInfo
{
	QueueType type;		// 队列类型
	uint32_t index;		// 所在队列族的索引
};

struct RHISwapchainInfo
{
	RHISurfaceRef surface;
	RHIQueueRef presentQueue;

	uint32_t imageCount;
	Extent2D extent;
	RHIFormat format;

};

struct RHICommandPoolInfo
{
	RHIQueueRef queue;

};

struct RHIBufferInfo
{
	uint64_t size;

	MemoryUsage memoryUsage = MEMORY_USAGE_GPU_ONLY;
	ResourceType type = RESOURCE_TYPE_BUFFER;

	BufferCreationFlags creationFlag = BUFFER_CREATION_NONE;

};

struct RHITextureInfo 
{
	RHIFormat format;
	Extent3D extent;
	uint32_t arrayLayers = 1;
	uint32_t mipLevels = 1;

	MemoryUsage memoryUsage = MEMORY_USAGE_GPU_ONLY;
	ResourceType type = RESOURCE_TYPE_TEXTURE;

	TextureCreationFlags creationFlag = TEXTURE_CREATION_NONE;

	friend bool operator== (const RHITextureInfo& a, const RHITextureInfo& b)
	{
		return  a.format == b.format &&
				a.extent == b.extent &&
				a.arrayLayers == b.arrayLayers &&
				a.mipLevels == b.mipLevels &&
				a.memoryUsage == b.memoryUsage &&
				a.type == b.type &&
				a.creationFlag == b.creationFlag;
	}

};

struct RHITextureViewInfo 
{
    RHITextureRef texture;
    RHIFormat format = FORMAT_UKNOWN;			// 此时取texture的format
	TextureViewType viewType = VIEW_TYPE_2D;

    TextureSubresourceRange subresource = {};	// 此时取texture的默认range

	friend bool operator== (const RHITextureViewInfo& a, const RHITextureViewInfo& b)
	{
		return  a.texture.get() == b.texture.get() &&
				a.format == b.format &&
				a.viewType == b.viewType &&
				a.subresource == b.subresource;
	}

};

struct RHISamplerInfo 
{
	FilterType minFilter = FILTER_TYPE_LINEAR;
    FilterType magFilter = FILTER_TYPE_LINEAR;
    MipMapMode mipmapMode = MIPMAP_MODE_LINEAR;
    AddressMode addressModeU = ADDRESS_MODE_REPEAT;
    AddressMode addressModeV = ADDRESS_MODE_REPEAT;
    AddressMode addressModeW = ADDRESS_MODE_REPEAT;
    CompareFunction compareFunction = COMPARE_FUNCTION_NEVER;
	SamplerReductionMode reductionMode = SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;

	float mipLodBias = 0.0f;			// Mip层的偏移，正值会让纹理更模糊，负值更锐利
	float maxAnisotropy = 0.0f;			// (Anisotroy = "各向异性过滤")

};

struct RHIShaderInfo
{
	std::string entry = "main";

	ShaderFrequency frequency;
	std::vector<uint8_t> code;
};

// 着色器绑定表
struct RHIShaderBindingTableInfo
{
	void AddRayGenGroup(RHIShaderRef rayGenShader) { rayGenGroups.push_back(rayGenShader); }
	void AddHitGroup(	RHIShaderRef closestHitShader,
						RHIShaderRef anyHitShader,
						RHIShaderRef intersectionShader) 
	{ 
		hitGroups.push_back({closestHitShader, anyHitShader, intersectionShader}); 
	}
	void AddMissGroup(RHIShaderRef rayMissShader) { missGroups.push_back(rayMissShader); }
	
	struct HitGroup
	{
		RHIShaderRef closestHitShader;			// 最近命中着色器
		RHIShaderRef anyHitShader;				// 任意命中着色器
		RHIShaderRef intersectionShader;		// 相交着色器
	};

	std::vector<RHIShaderRef> rayGenGroups;		// 光线生成的入口点,决定光线的 from 和 to
	std::vector<HitGroup> hitGroups;
	std::vector<RHIShaderRef> missGroups;

};

struct RHITopLevelAccelerationStructureInfo
{
	uint32_t maxInstance;
	std::vector<RHIAccelerationStructureInstanceInfo> instanceInfos;

};

struct RHIBottomLevelAccelerationStructureInfo
{
	RHIBufferRef vertexBuffer;
	RHIBufferRef indexBuffer;
	uint32_t triangleNum;
	uint32_t vertexStride = 0;
	uint32_t indexOffset = 0;
	uint32_t vertexOffset = 0;

};

// 着色器资源绑定条目
struct ShaderResourceEntry 
{
	// std::string name;

    uint32_t set = 0;
    uint32_t binding = 0;
	uint32_t size = 1;			// 数组大小
	ShaderFrequency frequency = SHADER_FREQUENCY_ALL;

	ResourceType type = RESOURCE_TYPE_NONE;
    // TextureViewType textureViewType = VIEW_TYPE_UNDEFINED;	// 只有反射会填的信息，创建描述符绑定并不需要

	friend bool operator== (const ShaderResourceEntry& a, const ShaderResourceEntry& b)
	{
		return  a.set == b.set &&
				a.binding == b.binding &&
				a.size == b.size &&
				a.frequency == b.frequency &&
				a.type == b.type;
				// a.textureViewType == b.textureViewType;
	}

};

struct ShaderReflectInfo
{
    std::string name;

    ShaderFrequency frequency;
    std::vector<ShaderResourceEntry> resources;
	std::unordered_set<std::string> definedSymbols;
	std::array<RHIFormat, MAX_SHADER_IN_OUT_VARIABLES> inputVariables = {};		// 按照location做索引
	std::array<RHIFormat, MAX_SHADER_IN_OUT_VARIABLES> outputVariables = {};
    uint32_t localSizeX = 0;
    uint32_t localSizeY = 0;
    uint32_t localSizeZ = 0;

	bool DefinedSymbol(std::string symbol) const { return definedSymbols.find(symbol) != definedSymbols.end(); }

};

struct PushConstantInfo
{
	uint32_t size = 128;
	ShaderFrequency frequency;
};

// Attachment = 声明的渲染目标
// Sampler = 绑定的纹理资源

struct AttachmentInfo
{
	// 附件绑定的纹理视图,即我们渲染目标本身
	RHITextureViewRef	textureView		= nullptr;	
	// 该附件当前所处的资源状态
	RHIResourceState	currentState = RESOURCE_STATE_UNDEFINED;

	// 解析模式，非RESOLVE_MODE_NONE表示将 MSAA 附件解析到 resolveTextureView
	ResolveMode			resolveMode = ResolveMode::RESOLVE_MODE_NONE;
	// 单采样的解析目标纹理视图
	RHITextureViewRef	resolveTextureView = nullptr;
	// 解析目标当前的资源状态，用于布局转换
	RHIResourceState	resolveCurrentState = RESOURCE_STATE_UNDEFINED;

	AttachmentLoadOp 	loadOp          = ATTACHMENT_LOAD_OP_DONT_CARE;
	AttachmentStoreOp	storeOp			= ATTACHMENT_STORE_OP_DONT_CARE;

	Color4				clearColor		= {0.0f, 0.0f, 0.0f, 0.0f};
	float				clearDepth 		= 1.0f;
	uint32_t			clearStencil 	= 0;
};

struct RHIRenderingInfo
{
	std::array<AttachmentInfo, MAX_RENDER_TARGETS> colorAttachments = {};
	//AttachmentInfo depthAttachment = {};
	//AttachmentInfo stencilAttachment = {};
	AttachmentInfo depthStencilAttachment = {};

	Extent2D extent = {};
	Offset2D offset = {};
	uint32_t layers = 1;

	RenderingFlags renderingFlags = 0;

	uint32_t multiviewCount = 0;
};

struct RHIRenderPassInfo
{
	std::array<AttachmentInfo, MAX_RENDER_TARGETS> colorAttachments = {};
	AttachmentInfo depthStencilAttachment = {};

	Extent2D extent = {0, 0};
	uint32_t layers = 1;

	uint32_t multiviewCount = 0;	

};

struct RHIRootSignatureInfo	
{
	// Sakura中使用了shader反射来获取全部的绑定资源信息，在函数CGPUUtil_InitRSParamTables中，
	// 因此初始化创建时只传了着色器而没有绑定的信息
	// 拆分一下把手动创建补上

	RHIRootSignatureInfo& AddPushConstant(const PushConstantInfo& pushConstant) { pushConstants.push_back(pushConstant); return *this; }
	RHIRootSignatureInfo& AddEntry(const ShaderResourceEntry& entry);
	RHIRootSignatureInfo& AddEntry(const RHIRootSignatureInfo& other);
	RHIRootSignatureInfo& AddEntryFromReflect(RHIShaderRef shader);
	const std::vector<PushConstantInfo>& GetPushConstants() const { return pushConstants; }
	const std::vector<ShaderResourceEntry>& GetEntries() const { return entries; }

protected:
	std::vector<ShaderResourceEntry> entries;
	std::vector<PushConstantInfo> pushConstants;
	
};

struct RHIDescriptorUpdateInfo	
{
	uint32_t binding = 0;
	uint32_t index = 0;

	ResourceType resourceType = RESOURCE_TYPE_NONE;		// 指明哪个成员有效

	RHIBufferRef buffer;
	RHITextureViewRef textureView;
	RHISamplerRef sampler;
	RHITopLevelAccelerationStructureRef tlas;

	uint64_t bufferOffset = 0;	// 仅buffer使用
	uint64_t bufferRange = 0;

};

struct RHIRasterizerStateInfo
{
	RasterizerFillMode fillMode = FILL_MODE_SOLID;
	RasterizerCullMode cullMode = CULL_MODE_BACK;
    RasterizerDepthClipMode depthClipMode = DEPTH_CLIP;

	float depthBias = 0.0f;
	float slopeScaleDepthBias = 0.0f;

	friend bool operator== (const RHIRasterizerStateInfo& a, const RHIRasterizerStateInfo& b)
	{
		return 	a.fillMode 				== b.fillMode &&
				a.cullMode 				== b.cullMode &&
				a.depthClipMode 		== b.depthClipMode &&
				a.depthBias 			== b.depthBias &&
				a.slopeScaleDepthBias 	== b.slopeScaleDepthBias;
	};

};

struct RHIDepthStencilStateInfo
{
	CompareFunction depthTest = COMPARE_FUNCTION_LESS_EQUAL;
	bool enableDepthTest = true;
	bool enableDepthWrite = true;

	bool __padding[2] = { 0 };

	// bool enableFrontFaceStencil = false;
	// CompareFunction frontFaceStencilTest = COMPARE_FUNCTION_ALWAYS;
	// StencilOp frontFaceStencilFailStencilOp = STENCIL_OP_KEEP;
	// StencilOp frontFaceDepthFailStencilOp = STENCIL_OP_KEEP;
	// StencilOp frontFacePassStencilOp = STENCIL_OP_KEEP;

	// bool enableBackFaceStencil = false;
	// CompareFunction backFaceStencilTest = COMPARE_FUNCTION_ALWAYS;
	// StencilOp backFaceStencilFailStencilOp = STENCIL_OP_KEEP;
	// StencilOp backFaceDepthFailStencilOp = STENCIL_OP_KEEP;
	// StencilOp backFacePassStencilOp = STENCIL_OP_KEEP;

	// uint8_t stencilReadMask = 0xFF;
	// uint8_t stencilWriteMask = 0xFF;
	
	friend bool operator== (const RHIDepthStencilStateInfo& a, const RHIDepthStencilStateInfo& b)
	{	
		return 	a.depthTest == b.depthTest &&
				a.enableDepthTest == b.enableDepthTest &&
				a.enableDepthWrite == b.enableDepthWrite;
				// a.enableFrontFaceStencil 		== b.enableFrontFaceStencil &&
				// a.frontFaceStencilTest 			== b.frontFaceStencilTest &&
				// a.frontFaceStencilFailStencilOp == b.frontFaceStencilFailStencilOp &&
				// a.frontFaceDepthFailStencilOp 	== b.frontFaceDepthFailStencilOp &&
				// a.frontFacePassStencilOp 		== b.frontFacePassStencilOp &&
				// a.enableBackFaceStencil 		== b.enableBackFaceStencil &&
				// a.backFaceStencilTest 			== b.backFaceStencilTest &&
				// a.backFaceStencilFailStencilOp 	== b.backFaceStencilFailStencilOp &&
				// a.backFaceDepthFailStencilOp 	== b.backFaceDepthFailStencilOp &&
				// a.backFacePassStencilOp 		== b.backFacePassStencilOp &&
				// a.stencilReadMask 				== b.stencilReadMask &&
				// a.stencilWriteMask 				== b.stencilWriteMask;
	}

};

struct RHIBlendStateInfo
{
	struct RenderTarget
	{
		BlendOp colorBlendOp = BLEND_OP_ADD;
		BlendFactor colorSrcBlend = BLEND_FACTOR_ONE;
		BlendFactor colorDstBlend = BLEND_FACTOR_ZERO;

		BlendOp alphaBlendOp = BLEND_OP_ADD;
		BlendFactor alphaSrcBlend = BLEND_FACTOR_ONE;
		BlendFactor alphaDstBlend = BLEND_FACTOR_ZERO;

		ColorWriteMasks colorWriteMask = COLOR_MASK_RGBA;

		bool enable = false;

		bool __padding[3] = { 0 };

		friend bool operator== (const RenderTarget& a, const RenderTarget& b)
		{
			return  a.colorBlendOp 		== b.colorBlendOp &&
					a.colorSrcBlend 	== b.colorSrcBlend &&
					a.colorDstBlend 	== b.colorDstBlend &&				
					a.alphaBlendOp 		== b.alphaBlendOp &&
					a.alphaSrcBlend 	== b.alphaSrcBlend &&
					a.alphaDstBlend 	== b.alphaDstBlend &&
					a.colorWriteMask 	== b.colorWriteMask &&
					a.enable 			== b.enable;
		}
	};

	std::array<RenderTarget, MAX_RENDER_TARGETS> renderTargets;
	
	friend bool operator== (const RHIBlendStateInfo& a, const RHIBlendStateInfo& b)
	{
		return a.renderTargets == b.renderTargets;
	}

};

struct VertexElement
{
	uint32_t streamIndex = 0;
	uint32_t attributeIndex = 0;
	RHIFormat format = FORMAT_UKNOWN;
	uint32_t offset = 0;
	uint32_t stride = 0;
	bool useInstanceIndex = false;

	bool __padding[3] = { 0 };

	friend bool operator== (const VertexElement& a, const VertexElement& b) 
	{
		return  a.streamIndex		    == b.streamIndex &&
				a.attributeIndex	    == b.attributeIndex &&		
				a.format			    == b.format &&
				a.offset			    == b.offset &&
				a.stride			    == b.stride &&
				a.useInstanceIndex    	== b.useInstanceIndex;
	}

};

struct VertexInputStateInfo
{
	std::vector<VertexElement> vertexElements;

	friend bool operator== (const VertexInputStateInfo& a, const VertexInputStateInfo& b)
	{
		if(a.vertexElements.size() != b.vertexElements.size()) return false;
		return a.vertexElements == b.vertexElements;
	}
};

//struct AttachmentFormatsInfo
//{
//	std::array<RHIFormat, MAX_RENDER_TARGETS> colorAttachmentFormats = { FORMAT_UKNOWN };
//	RHIFormat					depthStencilAttachmentFormat = FORMAT_UKNOWN;
//
//	friend bool operator==(const AttachmentFormatsInfo& a, const AttachmentFormatsInfo& b)
//	{
//		return a.colorAttachmentFormats == b.colorAttachmentFormats &&
//			a.depthStencilAttachmentFormat == b.depthStencilAttachmentFormat;
//	}
//};

struct RHIGraphicsPipelineInfo
{
	RHIShaderRef					vertexShader;
	RHIShaderRef					geometryShader;
	RHIShaderRef	 				fragmentShader;

	RHIRootSignatureRef				rootSignature;

	VertexInputStateInfo            vertexInputState = {};
	PrimitiveType					primitiveType = PRIMITIVE_TYPE_TRIANGLE_LIST;
	RHIRasterizerStateInfo			rasterizerState = {};
	RHIBlendStateInfo				blendState = {};
	RHIDepthStencilStateInfo		depthStencilState = {};

	// 这个就是所需的附件格式信息
	std::array<RHIFormat, MAX_RENDER_TARGETS> colorAttachmentFormats = { FORMAT_UKNOWN };
	RHIFormat						depthStencilAttachmentFormat = FORMAT_UKNOWN;

	uint32_t viewMask 				= 0b00000000;	// multiview

	// uint32_t						numSamples = 1;		// TODO

	friend bool operator== (const RHIGraphicsPipelineInfo& a, const RHIGraphicsPipelineInfo& b)
	{
		return  a.vertexShader.get() == b.vertexShader.get() &&
				a.geometryShader.get() == b.geometryShader.get() &&
				a.fragmentShader.get() == b.fragmentShader.get() &&
				a.rootSignature.get() == b.rootSignature.get() &&
				a.vertexInputState == b.vertexInputState &&
				a.primitiveType == b.primitiveType &&
				a.rasterizerState == b.rasterizerState &&
				a.blendState == b.blendState &&
				a.depthStencilState == b.depthStencilState &&
				a.colorAttachmentFormats == b.colorAttachmentFormats &&              
				a.depthStencilAttachmentFormat == b.depthStencilAttachmentFormat &&   
				a.viewMask == b.viewMask;
	}

};

struct RHIComputePipelineInfo
{
	RHIShaderRef 					computeShader;

	RHIRootSignatureRef				rootSignature;

	friend bool operator== (const RHIComputePipelineInfo& a, const RHIComputePipelineInfo& b)
	{
		return  a.computeShader.get() == b.computeShader.get() &&
				a.rootSignature.get() == b.rootSignature.get();
	}

};

struct RHIRayTracingPipelineInfo
{
	RHIShaderBindingTableRef 		shaderBindingTable;

	RHIRootSignatureRef				rootSignature;

	friend bool operator== (const RHIRayTracingPipelineInfo& a, const RHIRayTracingPipelineInfo& b)
	{
		return  a.shaderBindingTable.get() == b.shaderBindingTable.get() &&
				a.rootSignature.get() == b.rootSignature.get();
	}

};

struct RHIBufferBarrier 
{
    RHIBufferRef buffer;
    RHIResourceState srcState;
    RHIResourceState dstState;

	uint32_t offset = 0;
	uint32_t size = 0;

};

struct RHITextureBarrier
{
	RHITextureRef texture;
    RHIResourceState srcState;
    RHIResourceState dstState;

	TextureSubresourceRange subresource = {};	// 此时取texture的默认range

};
