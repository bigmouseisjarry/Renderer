
struct IndirectSetting
{
    uint processSize;              
    uint pipelineStateSize;        
    uint _padding0[2];

    uint drawSize;                
    uint frustumCull;             
    uint occlusionCull;          
    uint _padding1;
};

struct IndirectMeshDrawInfo
{
    uint objectID;			      
    uint commandID;				   
};

struct IndirectClusterDrawInfo
{
    uint objectID;                 
    uint clusterID;              
    uint commandID;				    
    uint _padding;
};

struct IndirectClusterGroupDrawInfo 
{
    uint objectID;                 
    uint clusterGroupID;           
    uint commandID;				    
    uint _padding;
};

layout(set = 1, binding = 0) buffer IndirectMeshDrawDatas { 

    IndirectSetting setting;
    IndirectMeshDrawInfo draws[MAX_PER_FRAME_OBJECT_SIZE];	

} MESH_DRAW_DATAS[];

layout(set = 1, binding = 1) buffer IndirectMeshDrawCommands { 

    RHIIndirectCommand commands[MAX_PER_FRAME_OBJECT_SIZE];

} MESH_DRAW_COMMANDS[];

layout(set = 1, binding = 2) buffer IndirectClusterDrawDatas { 

    IndirectSetting setting;
    IndirectClusterDrawInfo draws[MAX_PER_FRAME_CLUSTER_SIZE];

} CLUSTER_DRAW_DATAS[];

layout(set = 1, binding = 3) buffer IndirectClusterDrawCommands			           
{
    RHIIndirectCommand command[MAX_PER_PASS_PIPELINE_STATE_COUNT];	 
                                                        
} CLUSTER_DRAW_COMMANDS[];

layout(set = 1, binding = 4) buffer IndirectClusterGroupDrawDatas
{
    IndirectSetting setting;
    IndirectClusterGroupDrawInfo draws[MAX_PER_FRAME_CLUSTER_GROUP_SIZE];
		    
} CLUSTER_GROUP_DRAW_DATAS[];

layout(push_constant) uniform CullingLodSetting {

	uint passType;	// 当前处理的meshpass种类
	uint passCount;	// pass数量

    float lodErrorRate;
	float lodErrorOffset;
	uint disableVirtualMeshCulling;	
	uint disableOcclusionCulling;
	uint disableFrustrumCulling;
	uint showBoundingBox;
	uint enableStatistics;
} SETTING;

#define DEPTH_PASS_INDEX 0
#define POINT_SHADOW_PASS_INDEX 1
#define DIRECTIONAL_SHADOW_PASS_INDEX 2
#define G_BUFFER_PASS_INDEX 3
#define FORWARD_PASS_INDEX 4
#define TRANSPARENT_PASS_INDEX 5

bool CheckLOD(BoundingSphere sphere, float error)
{
	float dist = max(length(sphere.centerRadius.xyz) - sphere.centerRadius.w, 0.0f) / sphere.centerRadius.w;	// 库里的error是个与物体大小无关的百分比度量

    return SETTING.lodErrorRate * dist + SETTING.lodErrorOffset >= error;
}
