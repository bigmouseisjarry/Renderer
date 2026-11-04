#ifndef GBUFFER_GLSL
#define GBUFFER_GLSL

#include "math.glsl"
#include "types.glsl"
#include "pbs.glsl"

// G-Buffer一文件（对位HRP GBuffer.hlsl）：通道打包coders + Encode2GBuffer/DecodeGBuffer双函数
// + GetSurfaceInfo材质采集（对位HRP光栅版，三调用点=opaque写/thin写/forward直通，只差产物去哪）。
// encode/decode/采集同居=布局与参数化唯一权威点；传参形态=值形式（vec4实参，调用侧内联texelFetch；
// 不需要的槽显式传vec4(0.0f)/0.0f，GLSL无null纹理故以此表达"不用"）。
// velocity/objectId是pass机械量非材质打包，不在encode/decode之列。

// 法线：八面体 + 每轴12bit定点位拼进 RGB（24bit），A通道整个腾给 specF/AO
vec3 EncodeGBufferNormal(vec3 n)
{
    vec2 o   = OctEncode(normalize(n)) * 0.5 + 0.5;          // [0,1]
    uvec2 kk = uvec2(o * 4095.0 + 0.5);                      // 12bit/轴，round对称量化
    vec3 p;
    p.xy = vec2(kk & 255u) / 255.0;                          // 低8bit → R/G
    p.z  = float(((kk.x >> 8) << 4) | (kk.y >> 8)) / 255.0;  // 双轴高4bit两个nibble拼B
    return p;
}

vec3 DecodeGBufferNormal(vec3 p)
{
    uvec3 a = uvec3(p * 255.0 + 0.5);                        // +0.5：UNORM读回的浮点误差防截断错位（B字节错1=nibble跨界跳变）
    uvec2 kk = (a.xy & 255u) + (uvec2(a.z >> 4, a.z & 15u) << 8);
    return OctDecode(vec2(kk) / 4095.0 * 2.0 - 1.0);
}

vec4 EncodeEmissionRGBM(vec3 e)
{
    float maxC = max(max(e.r, e.g), max(e.b, 1e-6));         // 防log2(0)
    return vec4(e / max(maxC, 1.0), clamp(log2(maxC) / 10.0, 0.0, 1.0));
}                                                            // maxC≤1→A钳0→解码乘1无损；>1024软削顶

vec3 DecodeEmissionRGBM(vec4 m)
{
    return m.rgb * exp2(m.a * 10.0);
}


void Encode2GBuffer(vec3 diffuse, float transparent, float roughness, vec3 F0, vec3 normal, vec3 emission, vec3 gnormal, float ao,
                    out vec4 diffuseTransparent, out vec4 normalSpecF, out vec4 emissionRGBM, out vec4 gnormalAO, out vec4 F0Roughness, float specF)
{
    diffuseTransparent = vec4(diffuse, transparent);         // diffuse=写入端已预乘(1-m)
    normalSpecF        = vec4(EncodeGBufferNormal(normal), specF);
    emissionRGBM       = EncodeEmissionRGBM(emission);
    gnormalAO          = vec4(EncodeGBufferNormal(gnormal), ao);
    F0Roughness        = vec4(F0, roughness);
}

// 薄透明形（+IOR target，R8编码(η-1)/2，与decode的*2+1对偶）
void Encode2GBuffer(vec3 diffuse, float transparent, float roughness, vec3 F0, vec3 normal, vec3 emission, vec3 gnormal, float ao, float ior,
                    out vec4 diffuseTransparent, out vec4 normalSpecF, out vec4 emissionRGBM, out vec4 gnormalAO, out vec4 F0Roughness, out float iorR8, float specF)
{
    Encode2GBuffer(diffuse, transparent, roughness, F0, normal, emission, gnormal, ao,
                   diffuseTransparent, normalSpecF, emissionRGBM, gnormalAO, F0Roughness, specF);
    iorR8 = (ior - 1.0f) * 0.5f;
}

// 不透明形（5 target）；光栅档外字段零填充（aniso/tangent/clearCoat/sheen/Ld/虹彩运行时参数）
SurfaceInfo DecodeGBuffer(vec4 diffuseTransparent, vec4 normalSpecF, vec4 emission, vec4 gnormalAO, vec4 F0Roughness)
{
    SurfaceInfo info;
    info.diffuse     = diffuseTransparent.rgb;              // 写入端已预乘(1-m)
    info.transparent = diffuseTransparent.a;
    info.normal      = DecodeGBufferNormal(normalSpecF.xyz);
    info.specF       = normalSpecF.a;
    info.emission    = DecodeEmissionRGBM(emission);
    info.gnormal     = DecodeGBufferNormal(gnormalAO.xyz);
    info.ao          = gnormalAO.w;
    info.F0          = F0Roughness.rgb;
    info.roughness   = F0Roughness.a;
    info.ior         = 1.0f;
    info.tangent     = vec3(0.0f);
    info.aniso       = 0.0f;
    info.iridescence = 0.0f;
    info.index2      = 0.0f;
    info.dinc        = 0.0f;
    info.clearCoat   = 0.0f;
    info.sheen       = 0.0f;
    info.Ld          = 0.0f;
    return info;
}

// 薄透明形（+IOR target，R8编码(η-1)/2的解码）
SurfaceInfo DecodeGBuffer(vec4 diffuseTransparent, vec4 normalSpecF, vec4 emission, vec4 gnormalAO, vec4 F0Roughness, float iorEncoded)
{
    SurfaceInfo info = DecodeGBuffer(diffuseTransparent, normalSpecF, emission, gnormalAO, F0Roughness);
    info.ior = iorEncoded * 2.0f + 1.0f;
    return info;
}

// ———— GetSurfaceInfo（对位HRP光栅版材质采集：一个函数，调用点只差产物去哪）————
// 吸收三份手搓Fetch块的公共体：材质贴图采样+useVertexColor+金属度拆分+（thin/forward）写入期虹彩烘焙。
// 留在调用侧：alphaClip discard与clusterInspect（pass机械量）。
// bakeIridescence：thin/forward传true=按当时视角烘焙薄膜F0并置specF（且仅当材质iridescence!=0，非虹彩薄材质走正常Schlick）；
// opaque传false（拆分定案：不透明路无薄膜）。S.iridescence（运行时求值通道）光栅档恒0，RT直构档将来启用。
// diffuse约定：albedo*(1-m)，不乘HRP的0.96。
SurfaceInfo GetSurfaceInfo(uint objectID, vec2 texCoord, vec3 worldPos, vec3 normal, vec4 tangent, vec3 vColor, bool bakeIridescence)
{
    Material material = FetchMaterial(objectID);
    vec4 albedo = FetchAlbedo(material, texCoord);

    if(material.useVertexColor != 0)
        albedo.xyz *= vColor;

    float metallic = FetchMetallic(material, texCoord);

    SurfaceInfo S;
    S.diffuse     = albedo.xyz * (1.0f - metallic);
    S.transparent = 1.0f - albedo.a;
    S.normal      = FetchNormal(material, texCoord, normal, tangent);
    S.gnormal     = normalize(normal);
    S.emission    = FetchEmission(material);            // TODO 自发光强度除法的历史悬案随采集收编
    S.ao          = FetchAO(material, texCoord);
    S.roughness   = FetchRoughness(material, texCoord);
    S.F0          = mix(vec3(0.04f), albedo.xyz, metallic);
    S.ior         = material.ior;
    S.index2      = material.index2;
    S.dinc        = material.dinc;
    S.specF       = 0.0f;

    if(bakeIridescence && material.iridescence != 0)
    {
        // 薄膜干涉F0：按当时视角烘焙（视角冻结是已知近似，SSR/RT镜面读到冻结色）
        vec3 V = normalize(CAMERA.pos.xyz - worldPos);
        float cosTheta1 = clamp(dot(S.normal, V), 0.0, 1.0);
        float cosTheta2 = sqrt(max(0.0, 1.0 - (1.0 - cosTheta1 * cosTheta1) / max(1e-7, material.ior * material.ior)));
        S.F0 = IridescenceFresnel(cosTheta1, cosTheta2, material.ior, material.index2, metallic, material.dinc);
        S.specF = 1.0f;
    }

    // 全量预留：RT直构档才有生产者
    S.tangent     = vec3(0.0f);
    S.aniso       = 0.0f;
    S.iridescence = 0.0f;
    S.clearCoat   = 0.0f;
    S.sheen       = 0.0f;
    S.Ld          = 0.0f;
    return S;
}

#endif
