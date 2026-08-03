#pragma once

#include "Eigen/Core"
#include "Eigen/Geometry"

#include <cmath>
#include <cstdint>

#define PI 3.14159265358979323846

// 注意:    Eigen的矩阵是行优先 row
//          glm的矩阵是列优先 column

typedef Eigen::Matrix2f Mat2;
typedef Eigen::Matrix3f Mat3;
typedef Eigen::Matrix4f Mat4;

typedef Eigen::Vector2f Vec2;
typedef Eigen::Vector3f Vec3;
typedef Eigen::Vector4f Vec4;

typedef Eigen::Vector2i IVec2;
typedef Eigen::Vector3i IVec3;
typedef Eigen::Vector4i IVec4;

typedef Eigen::Vector2<uint32_t> UVec2;
typedef Eigen::Vector3<uint32_t> UVec3;
typedef Eigen::Vector4<uint32_t> UVec4;

typedef Eigen::Quaternion<float> Quaternion;

namespace Math 
{
    inline float ToRadians(float angle) { return angle * PI / 180.0f; }
    inline Vec2 ToRadians(Vec2 angle) { return angle * PI / 180.0f; }
    inline Vec3 ToRadians(Vec3 angle) { return angle * PI / 180.0f; }
    inline Vec4 ToRadians(Vec4 angle) { return angle * PI / 180.0f; }

    inline float ToAngle(float radians) { return radians * 180.0f / PI; }
    inline Vec2 ToAngle(Vec2 radians) { return radians * 180.0f / PI; }
    inline Vec3 ToAngle(Vec3 radians) { return radians * 180.0f / PI; }
    inline Vec4 ToAngle(Vec4 radians) { return radians * 180.0f / PI; }

    inline uint32_t Align(uint32_t value, uint32_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    //UE5 MicrosoftPlatformMath.h

    inline bool IsNaN(float A) { return _isnan(A) != 0; }
    inline bool IsNaN(double A) { return _isnan(A) != 0; }
    inline bool IsFinite(float A) { return _finite(A) != 0; }
    inline bool IsFinite(double A) { return _finite(A) != 0; }

    inline uint64_t CountLeadingZeros64(uint64_t Value)
    {
        //https://godbolt.org/z/Ejh5G4vPK	
        // return 64 if value if was 0
        unsigned long BitIndex;
        if (!_BitScanReverse64(&BitIndex, Value)) BitIndex = -1;
        return 63 - BitIndex;
    }

    inline uint64_t CountTrailingZeros64(uint64_t Value)
    {
        // return 64 if Value is 0
        unsigned long BitIndex;	// 0-based, where the LSB is 0 and MSB is 63
        return _BitScanForward64(&BitIndex, Value) ? BitIndex : 64;
    }

    inline uint32_t CountLeadingZeros(uint32_t Value)
    {
        // return 32 if value is zero
        unsigned long BitIndex;
        _BitScanReverse64(&BitIndex, uint64_t(Value) * 2 + 1);
        return 32 - BitIndex;
    }

    inline uint64_t CeilLogTwo64(uint64_t Arg)
    {
        // if Arg is 0, change it to 1 so that we return 0
        Arg = Arg ? Arg : 1;
        return 64 - CountLeadingZeros64(Arg - 1);
    }

    inline uint32_t FloorLog2(uint32_t Value)
    {
        // Use BSR to return the log2 of the integer
        // return 0 if value is 0
        unsigned long BitIndex;
        return _BitScanReverse(&BitIndex, Value) ? BitIndex : 0;
    }
    inline uint8_t CountLeadingZeros8(uint8_t Value)
    {
        unsigned long BitIndex;
        _BitScanReverse(&BitIndex, uint32_t(Value) * 2 + 1);
        return uint8_t(8 - BitIndex);
    }

    inline uint32_t CountTrailingZeros(uint32_t Value)
    {
        // return 32 if value was 0
        unsigned long BitIndex;	// 0-based, where the LSB is 0 and MSB is 31
        return _BitScanForward(&BitIndex, Value) ? BitIndex : 32;
    }

    inline uint32_t CeilLogTwo(uint32_t Arg)
    {
        // if Arg is 0, change it to 1 so that we return 0
        Arg = Arg ? Arg : 1;
        return 32 - CountLeadingZeros(Arg - 1);
    }

    inline uint32_t CeilDivide(uint32_t Arg, uint32_t Divider)
    {
        return (Arg + Divider - 1) / Divider;
    }

    inline uint32_t FloorDivide(uint32_t Arg, uint32_t Divider)
    {
        return Arg / Divider;
    }

    inline uint32_t RoundUpToPowerOfTwo(uint32_t Arg)
    {
        return 1 << CeilLogTwo(Arg);
    }

    inline uint64_t RoundUpToPowerOfTwo64(uint64_t Arg)
    {
        return uint64_t(1) << CeilLogTwo64(Arg);
    }

    inline uint64_t FloorLog2_64(uint64_t Value)
    {
        unsigned long BitIndex;
        return _BitScanReverse64(&BitIndex, Value) ? BitIndex : 0;
    }

    inline float Mode(float arg, float mode)
    {
        return arg - floorf(arg / mode) * mode;
    }

    Vec3 ClampEulerAngle(Vec3 angle);

    Vec3 ToEulerAngle(const Quaternion& q);  

    Quaternion ToQuaternion(Vec3 eulerAngle);

    Mat4 LookAt(Vec3 eye, Vec3 center, Vec3 up);

    Mat4 Perspective(float fovy, float aspect, float near, float far);

    Mat4 Ortho(float left, float right, float bottom, float top, float near, float far);

    void Mat3x4(Mat4 mat, float* newMat);

    Vec3 GetScale(const Mat4 &matrix);
}