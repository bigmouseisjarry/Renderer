#ifndef PBS_GLSL
#define PBS_GLSL

#include "math.glsl"
#include "types.glsl"

//辐射度量学
//辐射能量				Q	radiant energy  		辐射的电磁能，焦耳
//辐射通量/辐射功率		 Φ	 radiant flux			单位时间的辐射能量，瓦特
//辐照度				E	irradiance				单位面积的辐射通量，瓦特/平方米				遵从Lambert's Law，余弦
//辐射率				L	radiance				单位垂直面积单位立体角的辐射通量，尼特		（定义时考虑了方向无关性，积分辐照度时加上余弦）

// =====================================================================================
// 一、结构体（对位HRP SurfaceInfo——我们用轻量BxDFContext+BRDFData；光照端材质参数仍散传）
// =====================================================================================

struct BxDFContext
{
	float NoV;
	float NoL;
	float VoL;
	float NoH;
	float VoH;
};

void Init( inout BxDFContext Context, vec3 N, vec3 V, vec3 L )
{
	Context.NoL = dot(N, L);
	Context.NoV = dot(N, V);
	Context.VoL = dot(V, L);
	float InvLenH = rsqrt( 2 + 2 * Context.VoL );
    Context.NoH = clamp(( Context.NoL + Context.NoV ) * InvLenH, 0, 1);
	Context.VoH = clamp(InvLenH + InvLenH * Context.VoL, 0, 1);
}

// 入口返回值：漫反射/镜面分离（等价HRP的PBS_DIFFUSE/PBS_SPECULAR type-flags，struct形态更GLSL）
struct BRDFData {
	vec3 diffuse;
	vec3 specular;
};

// =====================================================================================
// 二、Helper（对位HRP Helper funcs；标[HRP]为本轮移植）
// =====================================================================================

#define POW_CLAMP 0.000001f
float ClampedPow(float X,float Y)
{
	return pow(max(abs(X),POW_CLAMP),Y);
}

float PhongShadingPow(float X, float Y)
{
	// The following clamping is done to prevent NaN being the result of the specular power computation.
	return ClampedPow(X, Y);
}

// [HRP] metallic → (1-F0)，漫反射能量配额
float OneMinusReflectivityFromMetallic(float metallic)
{
	float oneMinusDielectricSpec = 1.0 - 0.04;
	return oneMinusDielectricSpec - metallic * oneMinusDielectricSpec;
}

// [HRP] albedo+metallic → F0与漫反射色的拆分（G-Buffer写入端语义）
void DiffuseAndSpecularFromMetallic(vec3 albedo, float metallic, out vec3 specColor, out float oneMinusReflectivity)
{
	specColor = mix(vec3(0.04, 0.04, 0.04), albedo, metallic);		// F0
	oneMinusReflectivity = OneMinusReflectivityFromMetallic(metallic);	// (1 - metallic) * (1 - F0dielectric)
}

// [HRP] aniso标度(-1..1) → 双轴粗糙度
vec2 CalculateAnisoRoughness(float roughness, float aniso)
{
	float k = sqrt(-0.9 * abs(aniso) + 1);
	k = aniso > 0 ? k : 1.0 / k;
	return min(vec2(1.0), max(vec2(0.008), vec2(roughness * k, roughness / k)));
}

// =====================================================================================
// 三、漫反射项
// =====================================================================================

vec3 Diffuse_Lambert( vec3 DiffuseColor )
{
	return DiffuseColor * (1 / PI);
}

// [Burley 2012, "Physically-Based Shading at Disney"]
vec3 Diffuse_Burley( vec3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
{
	float FD90 = 0.5 + 2 * VoH * VoH * Roughness;
	float FdV = 1 + (FD90 - 1) * pow(1 - NoV, 5);
	float FdL = 1 + (FD90 - 1) * pow(1 - NoL, 5);
	return DiffuseColor * ( (1 / PI) * FdV * FdL );
}

// [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
vec3 Diffuse_OrenNayar( vec3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
{
	float a = Roughness * Roughness;
	float s = a;
	float s2 = s * s;
	float VoL = 2 * VoH * VoH - 1;		// double angle identity
	float Cosri = VoL - NoV * NoL;
	float C1 = 1 - 0.5 * s2 / (s2 + 0.33);
	float C2 = 0.45 * s2 / (s2 + 0.09) * Cosri * ( Cosri >= 0 ? rcp( max( NoL, NoV ) ) : 1 );
	return DiffuseColor / PI * ( C1 + C2 ) * ( 1 + Roughness * 0.5 );
}

// [Gotanda 2014, "Designing Reflectance Models for New Consoles"]
vec3 Diffuse_Gotanda( vec3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
{
	float a = Roughness * Roughness;
	float a2 = a * a;
	float F0 = 0.04;
	float VoL = 2 * VoH * VoH - 1;		// double angle identity
	float Cosri = VoL - NoV * NoL;

	float a2_13 = a2 + 1.36053;
	float Fr = ( 1 - ( 0.542026*a2 + 0.303573*a ) / a2_13 ) * ( 1 - pow( 1 - NoV, 5 - 4*a2 ) / a2_13 ) * ( ( -0.733996*a2*a + 1.50912*a2 - 1.16402*a ) * pow( 1 - NoV, 1 + rcp(39*a2*a2+1) ) + 1 );
	float Lm = ( max( 1 - 2*a, 0 ) * ( 1 - pow( 1 - NoL, 5) ) + min( 2*a, 1 ) ) * ( 1 - 0.5*a + 0.5*a * NoL );
	float Vd = ( a2 / ( (a2 + 0.09) * (1.31072 + 0.995584 * NoV) ) ) * ( 1 - pow( 1 - NoL, ( 1 - 0.3726732 * NoV * NoV ) / ( 0.188566 + 0.38841 * NoV ) ) );
	float Bp = Cosri < 0 ? 1.4 * NoV * Cosri : Cosri / max( NoL, 1e-8 );
	float Lr = (21.0 / 20.0) * (1 - F0) * ( Fr * Lm + Vd + Bp );
	return DiffuseColor / PI * Lr;
}

// =====================================================================================
// 四、法线分布项NDF
// =====================================================================================

// [Blinn 1977, "Models of light reflection for computer synthesized pictures"]
float D_Blinn( float a2, float NoH )
{
	float n = 2 / a2 - 2;
	return (n+2) / (2*PI) * PhongShadingPow( NoH, n );		// 1 mad, 1 exp, 1 mul, 1 log
}

// [Beckmann 1963, "The scattering of electromagnetic waves from rough surfaces"]
float D_Beckmann( float a2, float NoH )
{
	float NoH2 = NoH * NoH;
	return exp( (NoH2 - 1) / (a2 * NoH2) ) / ( PI * a2 * NoH2 * NoH2 );
}

// GGX / Trowbridge-Reitz
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float D_GGX( float a2, float NoH )
{
	float d = ( NoH * a2 - NoH ) * NoH + 1;	// 2 mad
	return a2 / ( PI*d*d );					// 4 mul, 1 rcp
}

// Anisotropic GGX
// [Burley 2012, "Physically-Based Shading at Disney"]
float D_GGXaniso( float ax, float ay, float NoH, float XoH, float YoH )
{
	float d = XoH*XoH / (ax*ax) + YoH*YoH / (ay*ay) + NoH*NoH;
	return 1.0f / ( PI * ax*ay * d*d );
}

float D_InvBlinn( float a2, float NoH )
{
	float A = 4;
	float Cos2h = NoH * NoH;
	float Sin2h = 1 - Cos2h;
	return rcp( PI * (1 + A*a2) ) * ( 1 + A * exp( -Cos2h / a2 ) );
}

float D_InvBeckmann( float a2, float NoH )
{
	float A = 4;
	float Cos2h = NoH * NoH;
	float Sin2h = 1 - Cos2h;
	float Sin4h = Sin2h * Sin2h;
	return rcp( PI * (1 + A*a2) * Sin4h ) * ( Sin4h + A * exp( -Cos2h / (a2 * Sin2h) ) );
}

float D_InvGGX( float a2, float NoH )
{
	float A = 4;
	float d = ( NoH - a2 * NoH ) * NoH + a2;
	return rcp( PI * (1 + A*a2) ) * ( 1 + 4 * a2*a2 / ( d*d ) );
}

// =====================================================================================
// 五、菲涅尔项（含虹彩整段——原iridescence.glsl归位，对位HRP"Iridescence Fresnel"节）
// =====================================================================================

// R(θ) = F0 + (1 − F0)(1 − cosθ)⁵   VoH
vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// NoV；对上式的一个近似，使得k_diffuse项与入射方向无关可以移出积分
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 F_None( vec3 SpecularColor )
{
	return SpecularColor;
}

// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
vec3 F_Schlick( vec3 SpecularColor, float VoH )
{
	float Fc = pow(1 - VoH, 5);					// 1 sub, 3 mul

	// Anything less than 2% is physically impossible and is instead considered to be shadowing
	return clamp(( 50.0 * SpecularColor.g ), 0, 1) * Fc + (1 - Fc) * SpecularColor;
}

vec3 F_Schlick(vec3 F0, vec3 F90, float VoH)
{
	float Fc = pow(1 - VoH, 5);
	return F90 * Fc + (1 - Fc) * F0;
}

vec3 F_AdobeF82(vec3 F0, vec3 F82, float VoH)
{
	// [Kutz et al. 2021, "Novel aspects of the Adobe Standard Material" ]
	// See Section 2.3 (note the formulas in the paper do not match the code, the code is the correct version)
	// The constants below are derived by just constant folding the terms dependent on CosThetaMax=1/7
	const float Fc = pow(1 - VoH, 5);
	const float K = 49.0 / 46656.0;
	vec3 b = (K - K * F82) * (7776.0 + 9031.0 * F0);
	return clamp((F0 + Fc * ((1 - F0) - b * (VoH - VoH * VoH))), 0, 1);
}

// 精确导体菲涅尔（从反射率反推n，走复折射率公式）
vec3 F_Fresnel( vec3 SpecularColor, float VoH )
{
	vec3 SpecularColorSqrt = sqrt( clamp(SpecularColor, vec3(0, 0, 0), vec3(0.99, 0.99, 0.99) ) );
	vec3 n = ( 1 + SpecularColorSqrt ) / ( 1 - SpecularColorSqrt );
	vec3 g = sqrt( n*n + VoH*VoH - 1 );
	return 0.5 * Square( (g - VoH) / (g + VoH) ) * ( 1 + Square( ((g+VoH)*VoH - 1) / ((g-VoH)*VoH + 1) ) );
}

// ---- 虹彩Fresnel（薄膜干涉，原iridescence.glsl）--------------------------------------
// 三层模型（空气η1=1 / 薄膜η2 / 基底η3+ik）
//
// dinc（膜厚）是调参标度：光程差 OPD = dinc * cosTheta2（物理式 2η2·d·cosθ2 的 2η2 系数已折叠进 dinc，Range 约 0-6，与物理 nm 不可直接互换）
//
// G-Buffer 用法（deferred.frag 烘焙）：写入端按当时视角 NoV 算一次，结果直接作为 F0 写入直存槽，
// specF=1 通知光照端跳过 Schlick 用平 F0。视角冻结是已知近似（SSR/RT 镜面打到虹彩面读到冻结色）。
//
// https://dl.acm.org/doi/epdf/10.1145/2990495

float iriSqr(float x) { return x * x; }
vec2  iriSqr(vec2 x)  { return x * x; }

// s/p 双偏振取平均（薄膜干涉是部分偏振光）
float depol(vec2 polV)            { return 0.5 * (polV.x + polV.y); }
vec3  depolColor(vec3 colS, vec3 colP) { return 0.5 * (colS + colP); }

// 界面菲涅尔（电介质），输出双偏振振幅反射率R与相位phi
void fresnelDielectric(float ct1, float n1, float n2, out vec2 R, out vec2 phi)
{
    n1 = max(n1, 1.0);
    n2 = max(n2, 1.0);
    float st1 = 1.0 - ct1 * ct1;    // sin²θ1
    float nr = n1 / n2;

    if(iriSqr(nr) * st1 > 1.0)      // 全反射
    {
        R = vec2(1.0);
        phi = 2.0 * atan(vec2(-iriSqr(nr) * sqrt(st1 - 1.0 / iriSqr(nr)) / ct1,
                              -sqrt(st1 - 1.0 / iriSqr(nr)) / ct1));
    }
    else                            // 透射+反射
    {
        float ct2 = sqrt(1.0 - iriSqr(nr) * st1);
        vec2 r = vec2((n2 * ct1 - n1 * ct2) / (n2 * ct1 + n1 * ct2),
                      (n1 * ct1 - n2 * ct2) / (n1 * ct1 + n2 * ct2));
        phi.x = (r.x < 0.0) ? PI : 0.0;     // 相位翻转藏在r的符号里
        phi.y = (r.y < 0.0) ? PI : 0.0;
        R = iriSqr(r);
    }
}

// 界面菲涅尔（导体，复折射率 n+ik）；k=0回退电介质公式防数值问题
void fresnelConductor(float ct1, float n1, float n2, float k, out vec2 R, out vec2 phi)
{
    if(k == 0.0)
    {
        fresnelDielectric(ct1, n1, n2, R, phi);
        return;
    }

    float A = iriSqr(n2) * (1.0 - iriSqr(k)) - iriSqr(n1) * (1.0 - iriSqr(ct1));
    float B = sqrt(iriSqr(A) + iriSqr(2.0 * iriSqr(n2) * k));
    float U = sqrt((A + B) / 2.0);
    float V = sqrt((B - A) / 2.0);

    R.y = (iriSqr(n1 * ct1 - U) + iriSqr(V)) / (iriSqr(n1 * ct1 + U) + iriSqr(V));
    phi.y = atan(2.0 * n1 * V * ct1, iriSqr(U) + iriSqr(V) - iriSqr(n1 * ct1)) + PI;

    R.x = (iriSqr(iriSqr(n2) * (1.0 - iriSqr(k)) * ct1 - n1 * U) + iriSqr(2.0 * iriSqr(n2) * k * ct1 - n1 * V))
        / (iriSqr(iriSqr(n2) * (1.0 - iriSqr(k)) * ct1 + n1 * U) + iriSqr(2.0 * iriSqr(n2) * k * ct1 + n1 * V));
    phi.x = atan(2.0 * n1 * iriSqr(n2) * ct1 * (2.0 * k * U - (1.0 - iriSqr(k)) * V),
                 iriSqr(iriSqr(n2) * (1.0 + iriSqr(k)) * ct1) - iriSqr(n1) * (iriSqr(U) + iriSqr(V)));
}

// CIE XYZ人眼敏感度曲线的高斯拟合，在光程差（傅里叶）域直接求值
vec3 evalSensitivity(float opd, float shift)
{
    float phase = 2.0 * PI * opd * 1.0e-6;
    vec3 val = vec3(5.4856e-13, 4.4201e-13, 5.2481e-13);
    vec3 pos = vec3(1.6810e+06, 1.7953e+06, 2.2084e+06);
    vec3 variance = vec3(4.3278e+09, 9.3046e+09, 6.6121e+09);
    vec3 xyz = val * sqrt(2.0 * PI * variance) * cos(pos * phase + shift) * exp(-variance * phase * phase);
    xyz.x += 9.7470e-14 * sqrt(2.0 * PI * 4.5282e+09) * cos(2.2399e+06 * phase + shift) * exp(-4.5282e+09 * phase * phase);
    return xyz / 1.0685e-7;
}

// Airy级数截断到m=0..3
// cosTheta1=入射角余弦(空气→薄膜)，cosTheta2=膜内折射角余弦(Snell)
// eta_2=薄膜折射率(=材质ior)，eta_3=基底折射率(=index2)，kappa_3=基底消光系数(传metallic)，d=膜厚标度(dinc)
vec3 IridescenceFresnel(float cosTheta1, float cosTheta2, float eta_2, float eta_3, float kappa_3, float d)
{
    // 界面1：空气→薄膜
    vec2 R12, phi12;
    fresnelDielectric(cosTheta1, 1.0, eta_2, R12, phi12);
    vec2 R21 = R12;
    vec2 T121 = 1.0 - R12;
    vec2 phi21 = PI - phi12;

    // 界面2：薄膜→基底
    vec2 R23, phi23;
    fresnelConductor(cosTheta2, eta_2, eta_3, kappa_3, R23, phi23);

    // 光程差与相位
    float OPD = d * cosTheta2;
    vec2 phi2 = phi21 + phi23;

    // 复合项
    vec3 I = vec3(0.0);
    vec2 R123 = R12 * R23;
    vec2 r123 = sqrt(R123);
    vec2 Rs = iriSqr(T121) * R23 / (1.0 - R123);

    // m=0 直流项
    vec2 C0 = R12 + Rs;
    vec3 S0 = evalSensitivity(0.0, 0.0);
    I += depol(C0) * S0;

    // m>0 干涉阶
    vec2 Cm = Rs - T121;
    for(int m = 1; m <= 3; ++m)
    {
        Cm *= r123;
        vec3 SmS = 2.0 * evalSensitivity(m * OPD, m * phi2.x);
        vec3 SmP = 2.0 * evalSensitivity(m * OPD, m * phi2.y);
        I += depolColor(Cm.x * SmS, Cm.y * SmP);
    }

    mat3 XYZ_TO_RGB = mat3(3.0799327, -1.537150, -0.542782,
                           -0.921235, 1.875992, 0.0452442,
                           0.0528909, -0.204043, 1.1511515);
    return clamp(I * XYZ_TO_RGB, 0.0, 1.0);
}

// =====================================================================================
// 六、几何函数项G，Vis合并了几何函数和归一化系数(4*NoL*NoV)
// =====================================================================================

float Vis_Implicit()
{
	return 0.25;
}

// [Neumann et al. 1999, "Compact metallic reflectance models"]
float Vis_Neumann( float NoV, float NoL )
{
	return 1 / ( 4 * max( NoL, NoV ) );
}

// [Kelemen 2001, "A microfacet based coupled specular-matte brdf model with importance sampling"]
float Vis_Kelemen( float VoH )
{
	return rcp( 4 * VoH * VoH + 1e-5);		// constant to prevent NaN
}

// Tuned to match behavior of Vis_Smith
// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
float Vis_Schlick( float a2, float NoV, float NoL )
{
	float k = sqrt(a2) * 0.5;
	float Vis_SchlickV = NoV * (1 - k) + k;
	float Vis_SchlickL = NoL * (1 - k) + k;
	return 0.25 / ( Vis_SchlickV * Vis_SchlickL );
}

// Smith term for GGX
// [Smith 1967, "Geometrical shadowing of a random rough surface"]
float Vis_Smith( float a2, float NoV, float NoL )
{
	float Vis_SmithV = NoV + sqrt( NoV * (NoV - NoV * a2) + a2 );
	float Vis_SmithL = NoL + sqrt( NoL * (NoL - NoL * a2) + a2 );
	return rcp( Vis_SmithV * Vis_SmithL );
}

// Appoximation of joint Smith term for GGX
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float Vis_SmithJointApprox( float a2, float NoV, float NoL )
{
	float a = sqrt(a2);
	float Vis_SmithV = NoL * ( NoV * ( 1 - a ) + a );
	float Vis_SmithL = NoV * ( NoL * ( 1 - a ) + a );
	return 0.5 * rcp( Vis_SmithV + Vis_SmithL );
}

// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float Vis_SmithJoint(float a2, float NoV, float NoL)
{
	float Vis_SmithV = NoL * sqrt(NoV * (NoV - NoV * a2) + a2);
	float Vis_SmithL = NoV * sqrt(NoL * (NoL - NoL * a2) + a2);
	return 0.5 * rcp(Vis_SmithV + Vis_SmithL);
}

// [HRP] 各向异性Smith联合项（与D_GGXaniso配套）
float Vis_SmithJointAniso(vec2 roughness, float NoV, float NoL, vec3 V, vec3 L, vec3 X, vec3 Y)
{
	float ax = roughness.x;
	float ay = roughness.y;
	float XoV = dot(X, V);
	float YoV = dot(Y, V);
	float XoL = dot(X, L);
	float YoL = dot(Y, L);
	float Vis_SmithV = NoL * length(vec3(ax * XoV, ay * YoV, NoV));
	float Vis_SmithL = NoV * length(vec3(ax * XoL, ay * YoL, NoL));
	return 0.5 * rcp(Vis_SmithV + Vis_SmithL);
}

// =====================================================================================
// 七、AO修正（[HRP]移植：几何法线驱动的遮蔽近似）
// =====================================================================================

float CalculateDiffuseAO(float ao, vec3 L, vec3 gN)
{
	float NdotL = max(0.0, dot(normalize(L), gN));
	return mix(ao, 1.0, NdotL * 0.8 + 0.2);
}

float CalculateSpecAO(float ao, float roughness, vec3 V, vec3 gN)
{
	return mix(ao + (1.0 - ao) * roughness * 0.8, 1.0, saturate(dot(V, gN) * 1.5 - 0.5));
}

// =====================================================================================
// 八、IBL
// =====================================================================================

vec3 PreFilteredReflection(vec3 R, float roughness, textureCube prefilteredMap, sampler sampl)
{
	const float MAX_REFLECTION_LOD = 8.0; // todo: param/const
	float lod = roughness * MAX_REFLECTION_LOD;
	float lodf = floor(lod);
	float lodc = ceil(lod);
	vec3 a = textureLod(samplerCube(prefilteredMap, sampl), R, lodf).xyz;
	vec3 b = textureLod(samplerCube(prefilteredMap, sampl), R, lodc).xyz;
	return mix(a, b, lod - lodf);
}

// =====================================================================================
// 九、入口（对位HRP：宏参选路PBS → BRDF核心；ResolveBRDF×2为兼容薄壳）
// =====================================================================================

// 宏参（对位HRP type flags；SS族(8/16)无消费者暂不设）
#define PBS_DIFFUSE  (1)
#define PBS_SPECULAR (2)
#define PBS_FULLY    (4)

// BRDF核心：SurfaceInfo直入，共享项一次算，漫/镜双项返回
// Physically based shading model
// parameterized with the below options
// [ Karis 2013, "Real Shading in Unreal Engine 4" slide 11 ]
// D = Microfacet NDF / G = Shadowing and masking / F = Fresnel
// Vis = G / (4*NoL*NoV)
// f = Microfacet specular BRDF = D*G*F / (4*NoL*NoV) = D*Vis*F
BRDFData BRDF(SurfaceInfo S, vec3 V, vec3 L)
{
	BRDFData data;
	data.diffuse = vec3(0.0f);
	data.specular = vec3(0.0f);

	BxDFContext context;
	Init(context, S.normal, V, L);

	if(context.NoL <= 0) return data;

	float a2 = Pow4(S.roughness);

	float D;
	float Vis;
	if(S.aniso != 0.0f)
	{
		// 各向异性分支（对位HRP；aniso=0材料不走此路零影响）
		vec3 X = S.tangent;
		vec3 Y = normalize(cross(X, S.normal));
		X = cross(S.normal, Y);
		vec2 r2 = CalculateAnisoRoughness(sqrt(a2), S.aniso);
		vec3 H = normalize(V + L);
		D = D_GGXaniso(r2.x, r2.y, context.NoH, dot(X, H), dot(Y, H));
		Vis = Vis_SmithJointAniso(r2, context.NoV, context.NoL, V, L, X, Y);
	}
	else
	{
		D = D_GGX(a2, context.NoH);
		Vis = Vis_Smith(a2, context.NoV, context.NoL);
	}

	vec3 F = S.specF > 0.5f ? S.F0 : F_Schlick(S.F0, context.VoH);
	// 虹彩运行时通道预留：S.iridescence>0.5时=IridescenceFresnel(NoV, cosTheta2, ior, index2, κ3, dinc)逐光路求值，RT档接入时启用

	vec3 k_diffuse = (vec3(1.0f) - F);							// 漫反射率=入射光折射配额

	data.diffuse  = k_diffuse * Diffuse_Lambert(S.diffuse);		// diffuse=已预乘(1-m)
	data.specular = D * Vis * F;

	return data;
}

// PBS入口（对位HRP：宏参选路 + gnormal驱动AO修正 + transparent能量折减 + 光色）
vec3 PBS(int type, SurfaceInfo S, vec3 L, vec3 lightColor, vec3 V)
{
	float diffuseAO = CalculateDiffuseAO(S.ao, L, S.gnormal);
	float specAO = CalculateSpecAO(S.ao, S.roughness, V, S.gnormal);
	S.diffuse *= (1.0f - S.transparent);						// 透射材质按透射率扣漫反射能量

	BRDFData data = BRDF(S, V, L);

	if(type == PBS_DIFFUSE)  return data.diffuse * lightColor * diffuseAO;
	if(type == PBS_SPECULAR) return data.specular * lightColor * specAO;
	if(type == PBS_FULLY)    return max(dot(S.normal, L), 0.0f) * (data.diffuse * diffuseAO + data.specular * specAO) * lightColor;
	return vec3(0.0f);
}

// 兼容薄壳：物理参数化版（金属度拆分，pt等RT直构路径）
BRDFData ResolveBRDF(vec3 albedo, float roughness, float metallic, vec3 N, vec3 V, vec3 L)
{
	SurfaceInfo S;
	S.normal = N;
	S.roughness = roughness;
	S.F0 = mix(vec3(0.04f), albedo, metallic);					// 光线垂直入射时的反射率
	S.diffuse = albedo * (1.0f - metallic);						// 预乘漫反射率
	S.specF = 0.0f;
	S.aniso = 0.0f;
	S.tangent = vec3(0.0f);
	return BRDF(S, V, L);
}

// 兼容薄壳：F0直存版（G-Buffer消费者）
// diffuse=写入端已预乘(1-m)的漫反射率，F0=写入端算好的镜面反射率
// specF=虹彩标志：非0时F直接用烘焙F0跳过Schlick（G-Buffer写入端已按当时视角烘焙薄膜干涉色）
BRDFData ResolveBRDF(vec3 diffuse, float roughness, vec3 F0, float specF, vec3 N, vec3 V, vec3 L)
{
	SurfaceInfo S;
	S.normal = N;
	S.roughness = roughness;
	S.F0 = F0;
	S.diffuse = diffuse;
	S.specF = specF;
	S.aniso = 0.0f;
	S.tangent = vec3(0.0f);
	return BRDF(S, V, L);
}

#endif
