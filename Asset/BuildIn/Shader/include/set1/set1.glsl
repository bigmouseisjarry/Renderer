#ifndef SET1_GLSL
#define SET1_GLSL
// 刀7a（2026-10-20）：set1=bindless资源池集（原bindless.glsl；刀1拆分自common.glsl，本轮重排连续化0-25）
// =数据表+顶点/纹理池+TLAS+全部Fetch*访问器；槽位宏见同目录defines.glsl
// 刀7b-3：自包含化——types.glsl依赖L1全局defines的MAX_*宏，必须先于types引入；
// 天空访问器已拆往../sky.glsl（依赖set0符号）
#include "defines.glsl"
#include "../defines.glsl"
#include "../math.glsl"
#include "../intersection.glsl"     // types.glsl的BoundingSphere/BoundingBox在此（L0伞序）
#include "../types.glsl"
#if(ENABLE_RAY_TRACING != 0)
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable
layout(set = 1, binding = SET1_BINDING_TLAS) uniform accelerationStructureEXT TLAS;
#endif
layout(set = 1, binding = SET1_BINDING_OBJECT) readonly buffer objects {

    Object slot[MAX_PER_FRAME_OBJECT_SIZE];

} OBJECTS;

layout(set = 1, binding = SET1_BINDING_MATERIAL) readonly buffer materials { 

    Material slot[MAX_PER_FRAME_RESOURCE_SIZE];

} MATERIALS;

layout(set = 1, binding = SET1_BINDING_LIGHT) buffer lights {

    DirectionalLight directionalLights[DIRECTIONAL_SHADOW_CASCADE_LEVEL];
    PointLight pointLights[MAX_POINT_LIGHT_COUNT];
    VolumeLight volumeLights[MAX_VOLUME_LIGHT_COUNT];
    LightSetting lightSetting;

} LIGHTS;

layout(set = 1, binding = SET1_BINDING_MESH_CARD) readonly buffer mesh_cards {

    MeshCardInfo slot[MAX_PER_FRAME_OBJECT_SIZE * 6];

} MESH_CARDS;

// 刀7b-4：mesh域两表自mesh_pass.glsl迁入（struct在types.glsl，宏guard与家族头共存）——
// mesh pass族改经set1.glsl取此声明，mesh_pass.glsl的set1段随7b-3退役
#ifndef MESH_CLUSTERS_DECLARED
#define MESH_CLUSTERS_DECLARED
layout(set = 1, binding = SET1_BINDING_MESH_CLUSTER) readonly buffer mesh_clusters {

    MeshCluster slot[MAX_PER_FRAME_CLUSTER_SIZE];

} MESH_CLUSTERS;
#endif

#ifndef MESH_CLUSTER_GROUPS_DECLARED
#define MESH_CLUSTER_GROUPS_DECLARED
layout(set = 1, binding = SET1_BINDING_MESH_CLUSTER_GROUP) readonly buffer mesh_cluster_groups {

    MeshClusterGroup slot[MAX_PER_FRAME_CLUSTER_SIZE];

} MESH_CLUSTER_GROUPS;
#endif

layout(set = 1, binding = SET1_BINDING_VERTEX) readonly buffer vertices {

    VertexStream slot[MAX_PER_FRAME_RESOURCE_SIZE];

} VERTICES;
layout(set = 1, binding = SET1_BINDING_BINDLESS_POSITION) readonly buffer positions { 

    float position[];

} POSITIONS[MAX_BINDLESS_RESOURCE_SIZE];

layout(set = 1, binding = SET1_BINDING_BINDLESS_NORMAL) readonly buffer normals { 

    float normal[];

} NORMALS[MAX_BINDLESS_RESOURCE_SIZE];


layout(set = 1, binding = SET1_BINDING_BINDLESS_TANGENT) readonly buffer tangents { 

    float tangent[];

} TANGENTS[MAX_BINDLESS_RESOURCE_SIZE];


layout(set = 1, binding = SET1_BINDING_BINDLESS_TEXCOORD) readonly buffer texCoords { 

    float texCoord[];

} TEXCOORDS[MAX_BINDLESS_RESOURCE_SIZE];


layout(set = 1, binding = SET1_BINDING_BINDLESS_COLOR) readonly buffer colors { 

    float color[];

} COLORS[MAX_BINDLESS_RESOURCE_SIZE];

layout(set = 1, binding = SET1_BINDING_BINDLESS_BONE_INDEX) readonly buffer boneIndexs { 

    int boneIndex[];

} BONEINDEXS[MAX_BINDLESS_RESOURCE_SIZE];

layout(set = 1, binding = SET1_BINDING_BINDLESS_BONE_WEIGHT) readonly buffer boneWeights { 

    float boneWeight[];

} BONEWEIGHTS[MAX_BINDLESS_RESOURCE_SIZE];

layout(set = 1, binding = SET1_BINDING_BINDLESS_ANIMATION) readonly buffer animations { 

    mat4 matrix[];

} ANIMATIONS[MAX_BINDLESS_RESOURCE_SIZE];

layout(set = 1, binding = SET1_BINDING_BINDLESS_INDEX) readonly buffer indices { 

    uint index[];

} INDICES[MAX_BINDLESS_RESOURCE_SIZE];

layout(set = 1, binding = SET1_BINDING_BINDLESS_SAMPLER) uniform sampler SAMPLER[];
layout(set = 1, binding = SET1_BINDING_BINDLESS_TEXTURE_1D) uniform texture1D TEXTURES_1D[];
layout(set = 1, binding = SET1_BINDING_BINDLESS_TEXTURE_1D_ARRAY) uniform texture1DArray TEXTURES_1D_ARRAY[];
layout(set = 1, binding = SET1_BINDING_BINDLESS_TEXTURE_2D) uniform texture2D TEXTURES_2D[];
layout(set = 1, binding = SET1_BINDING_BINDLESS_TEXTURE_2D_ARRAY) uniform texture2DArray TEXTURES_2D_ARRAY[];
layout(set = 1, binding = SET1_BINDING_BINDLESS_TEXTURE_CUBE) uniform textureCube TEXTURES_CUBE[];
layout(set = 1, binding = SET1_BINDING_BINDLESS_TEXTURE_3D) uniform texture3D TEXTURES_3D[];
mat4 FetchModel(in uint objectID)
{
    return OBJECTS.slot[objectID].model;
}

mat4 FetchPrevModel(in uint objectID)
{
    return OBJECTS.slot[objectID].prevModel;
}

uint FetchIndex(in uint objectID, in uint offset)
{
    uint indexID = OBJECTS.slot[objectID].indexID;
    uint index = INDICES[indexID].index[offset];

    return index;
}

Vertex FetchVertex(in uint vertexID, in uint index)
{
    Vertex vert;
    vert.pos = vec4(0.0f);
    vert.normal = vec3(0.0f);
    vert.tangent = vec4(0.0f);
    vert.color = vec3(0.0f);
    vert.texCoord = vec2(0.0f);

    if(vertexID == 0) return vert;
    VertexStream vertexStream = VERTICES.slot[vertexID];

    if(vertexStream.positionID > 0)
        vert.pos = vec4(POSITIONS[vertexStream.positionID].position[3 * index], 
                        POSITIONS[vertexStream.positionID].position[3 * index + 1], 
                        POSITIONS[vertexStream.positionID].position[3 * index + 2],
                        1.0f);

    if(vertexStream.normalID > 0)
        vert.normal = vec3( NORMALS[vertexStream.normalID].normal[3 * index], 
                            NORMALS[vertexStream.normalID].normal[3 * index + 1], 
                            NORMALS[vertexStream.normalID].normal[3 * index + 2]);  

    if(vertexStream.tangentID > 0)
        vert.tangent = vec4(TANGENTS[vertexStream.tangentID].tangent[4 * index], 
                            TANGENTS[vertexStream.tangentID].tangent[4 * index + 1], 
                            TANGENTS[vertexStream.tangentID].tangent[4 * index + 2],
                            TANGENTS[vertexStream.tangentID].tangent[4 * index + 3]);   

    if(vertexStream.colorID > 0)
        vert.color = vec3(  COLORS[vertexStream.colorID].color[3 * index], 
                            COLORS[vertexStream.colorID].color[3 * index + 1],
                            COLORS[vertexStream.colorID].color[3 * index + 2]);

    if(vertexStream.texCoordID > 0)       
        vert.texCoord = vec2(   TEXCOORDS[vertexStream.texCoordID].texCoord[2 * index], 
                                TEXCOORDS[vertexStream.texCoordID].texCoord[2 * index + 1]);                 
    
    return vert;
}

vec4 FetchVertexPos(in uint vertexID, in uint index)
{
    if(vertexID == 0) return vec4(0.0f);

    uint positionID = VERTICES.slot[vertexID].positionID;
    if(positionID == 0) return vec4(0.0f);

    return vec4(POSITIONS[positionID].position[3 * index], 
                POSITIONS[positionID].position[3 * index + 1], 
                POSITIONS[positionID].position[3 * index + 2],
                1.0f);
}

vec3 FetchVertexNormal(in uint vertexID, in uint index)
{
    if(vertexID == 0) return vec3(0.0f, 0.0f, 1.0f);

    uint normalID = VERTICES.slot[vertexID].normalID;
    if(normalID == 0) return vec3(0.0f, 0.0f, 1.0f);

    return vec3(NORMALS[normalID].normal[3 * index], 
                NORMALS[normalID].normal[3 * index + 1], 
                NORMALS[normalID].normal[3 * index + 2]);   
}

vec4 FetchVertexTangent(in uint vertexID, in uint index)
{
    if(vertexID == 0) return vec4(0.0f);

    uint tangentID = VERTICES.slot[vertexID].tangentID;
    if(tangentID == 0) return vec4(0.0f);

    return vec4(TANGENTS[tangentID].tangent[4 * index], 
                TANGENTS[tangentID].tangent[4 * index + 1], 
                TANGENTS[tangentID].tangent[4 * index + 2],
                TANGENTS[tangentID].tangent[4 * index + 3]);   
}

vec2 FetchVertexTexCoord(in uint vertexID, in uint index)
{
    if(vertexID == 0) return vec2(0.0f);

    uint texCoordID = VERTICES.slot[vertexID].texCoordID;
    if(texCoordID == 0) return vec2(0.0f);

    return vec2(TEXCOORDS[texCoordID].texCoord[2 * index], 
                TEXCOORDS[texCoordID].texCoord[2 * index + 1]);
}

vec3 FetchVertexColor(in uint vertexID, in uint index)
{
    if(vertexID == 0) return vec3(1.0f);

    uint colorID = VERTICES.slot[vertexID].colorID;
    if(colorID == 0) return vec3(1.0f);

    return vec3(COLORS[colorID].color[3 * index], 
                COLORS[colorID].color[3 * index + 1],
                COLORS[colorID].color[3 * index + 2]);
}

uvec4 FetchVertexBoneIndex(in uint vertexID, in uint index)
{
    if(vertexID == 0) return uvec4(0);

    uint boneIndexID = VERTICES.slot[vertexID].boneIndexID;
    if(boneIndexID == 0) return uvec4(0);

    return uvec4(BONEINDEXS[boneIndexID].boneIndex[4 * index], 
                 BONEINDEXS[boneIndexID].boneIndex[4 * index + 1], 
                 BONEINDEXS[boneIndexID].boneIndex[4 * index + 2],
                 BONEINDEXS[boneIndexID].boneIndex[4 * index + 3]);   
}

vec4 FetchVertexBoneWeight(in uint vertexID, in uint index)
{
    if(vertexID == 0) return vec4(0);

    uint boneWeightID = VERTICES.slot[vertexID].boneWeightID;
    if(boneWeightID == 0) return vec4(0);

    return vec4(BONEWEIGHTS[boneWeightID].boneWeight[4 * index], 
                BONEWEIGHTS[boneWeightID].boneWeight[4 * index + 1], 
                BONEWEIGHTS[boneWeightID].boneWeight[4 * index + 2],
                BONEWEIGHTS[boneWeightID].boneWeight[4 * index + 3]);   
}

vec4 FetchPos(in uint objectID, in uint index)
{
    uint vertexID = OBJECTS.slot[objectID].vertexID;
    return FetchVertexPos(vertexID, index);
}

vec3 FetchNormal(in uint objectID, in uint index)
{
    uint vertexID = OBJECTS.slot[objectID].vertexID;
    return FetchVertexNormal(vertexID, index);
}

vec4 FetchTangent(in uint objectID, in uint index)
{
    uint vertexID = OBJECTS.slot[objectID].vertexID;
    return FetchVertexTangent(vertexID, index);
}

vec3 FetchWorldNormal(in vec3 normal, in mat4 model)
{
    mat3 tbnModel = mat3(model);
    return normalize(tbnModel * normal);
}

vec4 FetchWorldTangent(in vec4 tangent, in mat4 model)
{
    mat3 tbnModel = mat3(model);
    return vec4(normalize(tbnModel * tangent.xyz), tangent.w);
}

vec2 FetchTexCoord(in uint objectID, in uint index)
{
    uint vertexID = OBJECTS.slot[objectID].vertexID;
    return FetchVertexTexCoord(vertexID, index);
}

vec3 FetchColor(in uint objectID, in uint index)
{
    uint vertexID = OBJECTS.slot[objectID].vertexID;
    return FetchVertexColor(vertexID, index);
}

uvec4 FetchBoneIndex(in uint objectID, in uint index)
{
    uint vertexID = OBJECTS.slot[objectID].vertexID;
    return FetchVertexBoneIndex(vertexID, index);  
}

vec4 FetchBoneWeight(in uint objectID, in uint index)
{
    uint vertexID = OBJECTS.slot[objectID].vertexID;
    return FetchVertexBoneWeight(vertexID, index); 
}

//光追顶点数据////////////////////////////////////////////////////////////////////////////

uvec3 FetchTriangleIndex(in uint objectID, in uint triangleID, in vec3 barycentrics)
{
	uint indexID = OBJECTS.slot[objectID].indexID;

	return uvec3(	INDICES[indexID].index[triangleID * 3],
					INDICES[indexID].index[triangleID * 3 + 1],
					INDICES[indexID].index[triangleID * 3 + 2]);
}

vec4 FetchTrianglePos(in uint objectID, in uvec3 triangleIndex, in vec3 barycentrics)
{
    return BarycentricsInterpolation(
		FetchPos(objectID, triangleIndex[0]),
		FetchPos(objectID, triangleIndex[1]),
		FetchPos(objectID, triangleIndex[2]),
		barycentrics);
}

vec3 FetchTriangleNormal(in uint objectID, in uvec3 triangleIndex, in vec3 barycentrics)
{
    return normalize(BarycentricsInterpolation(
		FetchNormal(objectID, triangleIndex[0]),
		FetchNormal(objectID, triangleIndex[1]),
		FetchNormal(objectID, triangleIndex[2]),
		barycentrics));
}

vec4 FetchTriangleTangent(in uint objectID, in uvec3 triangleIndex, in vec3 barycentrics)
{
    return BarycentricsInterpolation(
		FetchTangent(objectID, triangleIndex[0]),
		FetchTangent(objectID, triangleIndex[1]),
		FetchTangent(objectID, triangleIndex[2]),
		barycentrics);
}

vec2 FetchTriangleTexCoord(in uint objectID, in uvec3 triangleIndex, in vec3 barycentrics)
{
    return BarycentricsInterpolation(
		FetchTexCoord(objectID, triangleIndex[0]),
		FetchTexCoord(objectID, triangleIndex[1]),
		FetchTexCoord(objectID, triangleIndex[2]),
		barycentrics);
}

vec3 FetchTriangleColor(in uint objectID, in uvec3 triangleIndex, in vec3 barycentrics)
{
    return BarycentricsInterpolation(
		FetchColor(objectID, triangleIndex[0]),
		FetchColor(objectID, triangleIndex[1]),
		FetchColor(objectID, triangleIndex[2]),
		barycentrics);
}

//texture////////////////////////////////////////////////////////////////////////////

vec4 FetchTex2D(in uint slot, in vec2 coord, in vec2 offset, in vec2 scale) {
	return texture(sampler2D(TEXTURES_2D[slot], SAMPLER[1]), offset + coord * scale);   
}

vec4 FetchTex2D(in uint slot, in vec2 coord, in vec2 offset, in vec2 scale, in float lod) {
	return textureLod(sampler2D(TEXTURES_2D[slot], SAMPLER[1]), offset + coord * scale, lod);   
}

vec4 FetchTex2D(in uint slot, in vec2 coord) {
	return texture(sampler2D(TEXTURES_2D[slot], SAMPLER[1]), coord);   
}

vec4 FetchTex2D(in uint slot, in vec2 coord, in float lod) {
	return textureLod(sampler2D(TEXTURES_2D[slot], SAMPLER[1]), coord, lod);   
}

vec4 FetchTexCube(in uint slot, in vec3 vector) {
	return texture(samplerCube(TEXTURES_CUBE[slot], SAMPLER[1]), vector);   
}

vec4 FetchTexCube(in uint slot, in vec3 vector, in float lod) {
	return textureLod(samplerCube(TEXTURES_CUBE[slot], SAMPLER[1]), vector, lod);   
}

vec4 FetchTex3D(in uint slot, in vec3 vector) {
	return texture(sampler3D(TEXTURES_3D[slot], SAMPLER[1]), vector);   
}

vec4 FetchTex3D(in uint slot, in vec3 vector, in float lod) {
	return textureLod(sampler3D(TEXTURES_3D[slot], SAMPLER[1]), vector, lod);   
}

//material////////////////////////////////////////////////////////////////////////////

Material FetchMaterial(in uint objectID) {
	return MATERIALS.slot[OBJECTS.slot[objectID].materialID]; 
}

int FetchMaterialInt(in Material material, in int index) {
	return material.ints[index]; 
}

float FetchMaterialFloat(in Material material, in int index) {
	return material.floats[index]; 
}

vec4 FetchMaterialColor(in Material material, in int index) {
	return material.colors[index]; 
}

vec4 FetchMaterialTex2D(in Material material, in int index, in vec2 coord) {
	return FetchTex2D(material.textureSlots2D[index], coord, material.textureOffset, material.textureScale);
}

vec4 FetchMaterialTex2D(in Material material, in int index, in vec2 coord, in float lod) {
	return FetchTex2D(material.textureSlots2D[index], coord, material.textureOffset, material.textureScale, lod);
}

vec4 FetchMaterialTexCube(in Material material, in int index, in vec3 vector) {
	return FetchTexCube(material.textureSlotsCube[index], vector);
}

vec4 FetchMaterialTexCube(in Material material, in int index, in vec3 vector, in float lod) {
	return FetchTexCube(material.textureSlotsCube[index], vector, lod);
}

vec4 FetchMaterialTex3D(in Material material, in int index, in vec3 vector) {
	return FetchTex3D(material.textureSlots3D[index], vector);
}

vec4 FetchMaterialTex3D(in Material material, in int index, in vec3 vector, in float lod) {
	return FetchTex3D(material.textureSlots3D[index], vector, lod);
}

vec4 FetchBaseColor(in Material material){
    return material.baseColor;  
}

float FetchRoughness(in Material material, in vec2 coord){
    if(material.textureArm > 0)        
    {
        vec3 arm = FetchTex2D(material.textureArm, coord, material.textureOffset, material.textureScale).xyz;
        arm = pow(arm, vec3(1.0/2.2));          //gamma矫正

        return arm.y;
    }
    else return clamp(material.roughness, 0.00001, 0.99999); 
}

float FetchMetallic(in Material material, in vec2 coord){
    if(material.textureArm > 0)        
    {
        vec3 arm = FetchTex2D(material.textureArm, coord, material.textureOffset, material.textureScale).xyz;
        arm = pow(arm, vec3(1.0/2.2));          //gamma矫正

        return arm.z;
    }
    else return clamp(material.metallic, 0.00001, 0.99999);   
}

float FetchAO(in Material material, in vec2 coord){
    if(material.textureArm > 0)        
    {
        vec3 arm = FetchTex2D(material.textureArm, coord, material.textureOffset, material.textureScale).xyz;
        arm = pow(arm, vec3(1.0/2.2));          //gamma矫正

        return arm.x;
    }
    else return 1.0f;   
}

vec3 FetchEmission(in Material material){
    return material.emission.xyz * material.emission.w; 
}

float FetchEmissionIntencity(in Material material){
    return material.emission.w; 
}

// 原始反照率 = baseColor × diffuse贴图
vec4 FetchAlbedo(in Material material, in vec2 coord) {
    if(material.textureDiffuse > 0)
    {
        vec4 albedo = FetchTex2D(material.textureDiffuse, coord, material.textureOffset, material.textureScale);
        albedo = pow(albedo, vec4(1.0/2.2));            //gamma矫正
        albedo = FetchBaseColor(material) * albedo;

        return albedo;
    }
    else return FetchBaseColor(material);
}

vec3 FetchNormal(in Material material, in vec2 coord, in vec3 normal, in vec4 tangent) {
	if(material.textureNormal > 0)     
    {
        //计算每像素的tbn矩阵可以避免在vert shader输出上的额外两个vec3的插值，其实还会更快！
        float fSign = tangent.w < 0 ? -1 : 1;
        highp vec3 n = normalize(normal);
        highp vec3 t = normalize(tangent.xyz - n * dot(n, tangent.xyz));
        highp vec3 b = fSign * normalize(cross(n, t));   // B = cross(N, T) * 手性

        highp mat3 TBN = mat3(t, b, n);

        vec3 texNormal = FetchTex2D(material.textureNormal, coord, material.textureOffset, material.textureScale).xyz;
        vec3 outNormal = normalize(texNormal * 2.0 - 1.0);  
        outNormal = normalize(TBN * outNormal);

        return outNormal;
    }

    else return normal;
}

vec4 FetchArm(in Material material, in vec2 coord) {
	if(material.textureArm > 0)        return FetchTex2D(material.textureArm, coord, material.textureOffset, material.textureScale);
    else return vec4(1.0f, 1.0f, 0.0f, 0.0f);
}

vec4 FetchSpecular(in Material material, in vec2 coord) {
	if(material.textureSpecular > 0)        return FetchTex2D(material.textureSpecular, coord, material.textureOffset, material.textureScale);
    else return vec4(0.0f, 0.0f, 0.0f, 0.0f);
}

//默认纹理////////////////////////////////////////////////////////////////////////////

// 刀7b-3：SkyboxTextureSize/FetchSkyLight×2（引用set0的GLOBAL_SETTING）已拆往../sky.glsl，
// 本文件自此自包含——不依赖set0任何声明，窄头用户可单独include（fxaa等只取SAMPLER的场景）

#endif
// 刀4a：静态资源随池迁入（槽位沿用set0旧号，仅set位0→1）
layout(set = 1, binding = SET1_BINDING_SKYBOX_IBL) uniform textureCube SKYBOX_IBL[2];    // diffuse, specular
layout(set = 1, binding = SET1_BINDING_STBN) uniform texture2D STBN[64];
