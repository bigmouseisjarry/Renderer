#include "Math.h"

namespace Math 
{
    Vec3 ClampEulerAngle(Vec3 angle)
    {
        Vec3 ret = angle;
        if(ret.z() > 89.9f) ret.z() = 89.9f;
        if(ret.z() < -89.9f) ret.z() = -89.9f;

        ret.y() = fmod(ret.y(), 360.0f);
        ret.z() = fmod(ret.z(), 360.0f);
        if(ret.y() > 180.0f) ret.y() -= 360.0f;
        if(ret.z() > 180.0f) ret.z() -= 360.0f;

        return ret;
    }

    // Vec3 ToEulerAngle(const Quaternion& q)
    // {
    //     return ToAngle(q.toRotationMatrix().eulerAngles(1, 2, 0));    // yaw = UnitY轴, pitch = UnitZ轴, roll = UnitX轴
    //                                                                   // 有奇异性问题
    // }

    // https://blog.csdn.net/WillWinston/article/details/125746107
    Vec3 ToEulerAngle(const Quaternion& q)   // 确保pitch的范围[-PI/2, PI/2]
    {                                               // yaw和roll都是[-PI, PI]
        double angles[3];

        // yaw (y-axis rotation)
        double sinr_cosp = 2 * (q.w() * q.y() -q.x() * q.z());
        double cosr_cosp = 1 - 2 * (q.y() * q.y() + q.z() * q.z());
        angles[0] = std::atan2(sinr_cosp, cosr_cosp);

        // pitch (z-axis rotation)
        double sinp = 2 * (q.w() * q.z() + q.x() * q.y());
        if (std::abs(sinp) >= 1)
            angles[1] = std::copysign(PI / 2, sinp); // use 90 degrees if out of range
        else
            angles[1] = std::asin(sinp);

        // roll (x-axis rotation)
        double siny_cosp = 2 * (q.w() * q.x() - q.y() * q.z());
        double cosy_cosp = 1 - 2 * (q.x() * q.x() + q.z() * q.z());
        angles[2] = std::atan2(siny_cosp, cosy_cosp);     

        angles[0] *= 180 / PI;  // 有一点点精度问题，可以忽略
        angles[1] *= 180 / PI;
        angles[2] *= 180 / PI; 

        return Vec3(angles[2], angles[0], angles[1]);
    }

    Quaternion ToQuaternion(Vec3 eulerAngle)
    {
        Vec3 radians = ToRadians(eulerAngle) ;  // 右手系，Y向上
        Eigen::AngleAxisf yaw = Eigen::AngleAxisf(radians(1),Vec3::UnitY()); 
        Eigen::AngleAxisf pitch = Eigen::AngleAxisf(radians(2),Vec3::UnitZ());  
        Eigen::AngleAxisf roll = Eigen::AngleAxisf(radians(0),Vec3::UnitX());

        return yaw * pitch * roll;  // 有顺序
    }

    // 下面这一组也是对的，旋转顺序不一样
    // Vec3 ToEulerAngle(const Quaternion& q)   // 确保pitch的范围[-PI/2, PI/2]
    // {                                               // yaw和roll都是[-PI, PI]
    //     double angles[3];

    //     // roll (x-axis rotation)
    //     double sinr_cosp = 2 * (q.w() * q.x() + q.y() * q.z());
    //     double cosr_cosp = 1 - 2 * (q.x() * q.x() + q.z() * q.z());
    //     angles[2] = std::atan2(sinr_cosp, cosr_cosp);

    //     // pitch (z-axis rotation)
    //     double sinp = 2 * (q.w() * q.z() - q.y() * q.x());
    //     if (std::abs(sinp) >= 1)
    //         angles[1] = std::copysign(PI / 2, sinp); // use 90 degrees if out of range
    //     else
    //         angles[1] = std::asin(sinp);

    //     // yaw (y-axis rotation)
    //     double siny_cosp = 2 * (q.w() * q.y() + q.x() * q.z());
    //     double cosy_cosp = 1 - 2 * (q.y() * q.y() + q.z() * q.z());
    //     angles[0] = std::atan2(siny_cosp, cosy_cosp);     

    //     angles[0] *= 180 / PI;  // 有一点点精度问题，可以忽略
    //     angles[1] *= 180 / PI;
    //     angles[2] *= 180 / PI; 

    //     return Vec3(angles[0], angles[1], angles[2]);
    // }

    // Quaternion ToQuaternion(Vec3 eulerAngle)
    // {
    //     Vec3 radians = ToRadians(eulerAngle) ;  // 右手系，Y向上
    //     Eigen::AngleAxisf yaw = Eigen::AngleAxisf(radians(0),Vec3::UnitY()); 
    //     Eigen::AngleAxisf pitch = Eigen::AngleAxisf(radians(1),Vec3::UnitZ());  
    //     Eigen::AngleAxisf roll = Eigen::AngleAxisf(radians(2),Vec3::UnitX());

    //     return roll * pitch * yaw;  // 有顺序
    // }

    Mat4 LookAt(Vec3 eye, Vec3 center, Vec3 up)
    {
        Vec3 f = (center - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        Vec3 u = s.cross(f);

        Mat4 mat = Mat4::Identity();
        mat(0, 0) = s.x();
        mat(0, 1) = s.y();
        mat(0, 2) = s.z();
        mat(1, 0) = u.x();
        mat(1, 1) = u.y();
        mat(1, 2) = u.z();
        mat(2, 0) =-f.x();
        mat(2, 1) =-f.y();
        mat(2, 2) =-f.z();
        mat(0, 3) =-s.dot(eye);
        mat(1, 3) =-u.dot(eye);
        mat(2, 3) = f.dot(eye);

        return mat;
    }

    Mat4 Perspective(float fovy, float aspect, float near, float far)
    {
        float const tanHalfFovy = tan(fovy / 2.0f);

        Mat4 mat = Mat4::Zero();
        mat(0, 0) = 1.0f / (aspect * tanHalfFovy);
        mat(1, 1) = 1.0f / (tanHalfFovy);
        mat(2, 2) = far / (near - far);
        mat(3, 2) = - 1.0f;
        mat(2, 3) = -(far * near) / (far - near);
        return mat;
    }

    Mat4 Ortho(float left, float right, float bottom, float top, float near, float far)
    {
        Mat4 mat = Mat4::Identity();
        mat(0, 0) = 2.0f / (right - left);
        mat(1, 1) = 2.0f / (top - bottom);
        mat(2, 2) = - 1.0f / (far - near);
        mat(0, 3) = - (right + left) / (right - left);
        mat(1, 3) = - (top + bottom) / (top - bottom);
        mat(2, 3) = - near / (far - near);
        return mat;
    }

    void Mat3x4(Mat4 mat, float* newMat)
    {
        newMat[0] = mat.row(0).x();
        newMat[1] = mat.row(0).y();
        newMat[2] = mat.row(0).z();
        newMat[3] = mat.row(0).w();
        newMat[4] = mat.row(1).x();
        newMat[5] = mat.row(1).y();
        newMat[6] = mat.row(1).z();
        newMat[7] = mat.row(1).w();
        newMat[8] = mat.row(2).x();
        newMat[9] = mat.row(2).y();
        newMat[10] = mat.row(2).z();
        newMat[11] = mat.row(2).w();
    }

    Vec3 GetScale(const Mat4 &matrix) 
    {
        Vec3 scale;
        scale.x() = std::sqrt(matrix(0, 0) * matrix(0, 0) + 
                            matrix(1, 0) * matrix(1, 0) + 
                            matrix(2, 0) * matrix(2, 0));

        scale.y() = std::sqrt(matrix(0, 1) * matrix(0, 1) + 
                            matrix(1, 1) * matrix(1, 1) + 
                            matrix(2, 1) * matrix(2, 1));

        scale.z() = std::sqrt(matrix(0, 2) * matrix(0, 2) + 
                            matrix(1, 2) * matrix(1, 2) + 
                            matrix(2, 2) * matrix(2, 2));

        return scale;
    }
}