#ifndef IRIDESCENCE_GLSL
#define IRIDESCENCE_GLSL

// 薄膜干涉（虹彩）Fresnel：三层模型（空气η1=1 / 薄膜η2 / 基底η3+ik）
//
// dinc（膜厚）是调参标度：光程差 OPD = dinc * cosTheta2（物理式 2η2·d·cosθ2 的 2η2 系数已折叠进 dinc，Range 约 0-6，与物理 nm 不可直接互换）
//
// G-Buffer 用法（deferred.frag 烘焙）：写入端按当时视角 NoV 算一次，结果直接作为 F0 写入直存槽，
// specF=1 通知光照端跳过 Schlick 用平 F0。视角冻结是已知近似（SSR/RT 镜面打到虹彩面读到冻结色）。

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

    // XYZ→RGB（字面量按行主序源抄录；GLSL mat3列主序，故用 I*M 等价 mul(M,I)）
    mat3 XYZ_TO_RGB = mat3(3.0799327, -1.537150, -0.542782,
                           -0.921235, 1.875992, 0.0452442,
                           0.0528909, -0.204043, 1.1511515);
    return clamp(I * XYZ_TO_RGB, 0.0, 1.0);
}

#endif
