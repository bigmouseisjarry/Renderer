#ifndef DDGI_GLSL
#define DDGI_GLSL


struct DDGIPayload
{
    float dist;
    vec4 normal;
    vec4 diffuse;
    vec4 emission;
};

// 相对坐标：probe小纹理内的坐标
// 绝对坐标：大纹理整体的坐标

// 一维下标到三维下标索引
ivec3 FetchProbeCoord(in DDGISetting ddgi, int probeIndex)
{
    ivec3 pos;
    pos.x = probeIndex % ddgi.probeCounts.x;
    pos.y = (probeIndex % (ddgi.probeCounts.x * ddgi.probeCounts.y)) / ddgi.probeCounts.x;
    pos.z = probeIndex / (ddgi.probeCounts.x * ddgi.probeCounts.y);

    return pos;
}

// 三维下标索引到世界空间坐标
vec3 FetchProbeWorldPos(in DDGISetting ddgi, ivec3 probeCoord)
{
    return ddgi.gridStep * vec3(probeCoord) + ddgi.gridStartPosition;
}

// 一维下标标索到世界空间坐标
vec3 FetchProbeWorldPos(in DDGISetting ddgi, int probeIndex)
{
    ivec3 probeCoord = FetchProbeCoord(ddgi, probeIndex);

    return FetchProbeWorldPos(ddgi, probeCoord);
}

// 世界空间到三维下标索引，下取整
ivec3 FetchBaseProbeCoord(in DDGISetting ddgi, vec3 worldPos) 
{
    return clamp(ivec3((worldPos - ddgi.gridStartPosition) / ddgi.gridStep), ivec3(0, 0, 0), ivec3(ddgi.probeCounts) - ivec3(1, 1, 1));
}

// 三维下标到一维下标索引
int FetchPorbeIndex(in DDGISetting ddgi, ivec3 probeCoords) 
{
    return int(probeCoords.x + probeCoords.y * ddgi.probeCounts.x + probeCoords.z * ddgi.probeCounts.x * ddgi.probeCounts.y);
}

// 绝对像素坐标到一维下标索引
int FetchPorbeIndex(vec2 texCoord, int fullTextureWidth, int probeSideLength)
{
    int probeWithBorderSide = probeSideLength + 2;
    int probesPerSide       = (fullTextureWidth - 2) / probeWithBorderSide;                                     //每行探针数目
    return int(texCoord.x / probeWithBorderSide) + probesPerSide * int(texCoord.y / probeWithBorderSide);       //一维偏移
}

// 绝对/相对像素坐标到相对UV坐标
vec2 FetchProbeRelativeUV(ivec2 texCoord, int probeSideLength)
{ 
    int probeWithBorderSide = probeSideLength + 2;

    vec2 octFragCoord = ivec2((texCoord.x - 2) % probeWithBorderSide, (texCoord.y - 2) % probeWithBorderSide);      //绝对/相对像素坐标到相对像素坐标
                
    return (vec2(octFragCoord) + vec2(0.5f)) * (2.0f / float(probeSideLength)) - vec2(1.0f, 1.0f);                  //相对像素坐标到相对UV坐标，加了半像素到像素中心(必须加)
                                                                                                                    //= normalizedOctCoord
}

// 根据法线及探针三维下标索引获取绝对纹理坐标
vec2 FetchProbeAbsTexCoord(vec3 dir, int probeIndex, int fullTextureWidth, int fullTextureHeight, int probeSideLength) 
{
    vec2 normalizedOctCoord = OctEncode(normalize(dir));
    vec2 normalizedOctCoordZeroOne = (normalizedOctCoord + vec2(1.0f)) * 0.5f;  // 八面体映射的相对像素坐标

    float probeWithBorderSide = probeSideLength + 2.0f;                         // 单个probe纹理的像素尺寸 + 2像素padding
                                                                                // 单个probe左上1像素padding，整个纹理左上1像素padding

    vec2 octCoordNormalizedToTextureDimensions = 
        vec2(normalizedOctCoordZeroOne * probeSideLength) / 
        vec2(fullTextureWidth, fullTextureHeight);                              // 相对像素坐标到绝对UV坐标

    int probesPerRow = (fullTextureWidth - 2) / int(probeWithBorderSide);

    vec2 probeTopLeftPosition = vec2(	(probeIndex % probesPerRow) * probeWithBorderSide,
                                        (probeIndex / probesPerRow) * probeWithBorderSide) +
                                        vec2(2.0f, 2.0f);                                       // 小纹理左上在整个纹理的像素坐标，包括了padding
                                                                                                // 小纹理按一维索引行优先排列

    vec2 normalizedProbeTopLeftPosition = vec2(probeTopLeftPosition) / vec2(fullTextureWidth, fullTextureHeight);  //

    return vec2(normalizedProbeTopLeftPosition + octCoordNormalizedToTextureDimensions);        //  左上UV偏移 + 小纹理内UV偏移
}





// 获取辐照度
vec3 FetchDDGIIrradiance(
    in DDGISetting ddgi, 
    vec3 worldPos, vec3 normal, vec3 viewVec, 
    texture2D irradianceTexture, 
    texture2D depthTexture,
	sampler sampl)
{
    ivec3 baseGridCoord = FetchBaseProbeCoord(ddgi, worldPos);      //下取整的探针索引
    vec3 baseProbePos = FetchProbeWorldPos(ddgi, baseGridCoord);    //探针坐标
    
    vec3  sumIrradiance = vec3(0.0f);
    float sumWeight = 0.0f;

    vec3 alpha = clamp((worldPos - baseProbePos) / ddgi.gridStep, vec3(0.0f), vec3(1.0f));   //距下取整坐标的三维重心坐标

    for (int i = 0; i < 8; ++i) 
    {
        //对包围的八个探针中的辐照度进行加权
        //权重包括三部分：
        //1. 三线性插值系数，着色点距离探针越远，权重越低
        //2. 方向系数，着色点到探针的表面法线越大，权重越低
        //3. 切比雪夫系数，着色点到探针之间有遮挡物的概率越大，权重越低

        float weight = 1.0;

        ivec3  offset           = ivec3(i, i >> 1, i >> 2) & ivec3(1);
        ivec3  probeGridCoord   = clamp(baseGridCoord + offset, ivec3(0), ddgi.probeCounts - ivec3(1));  //循环测试的探针索引
        int probeIdx            = FetchPorbeIndex(ddgi, probeGridCoord);
        vec3 probePos           = FetchProbeWorldPos(ddgi, probeGridCoord);

        //2. 方向系数
        {
            vec3 directionToProbe = normalize(probePos - worldPos);
            //weight *= max(0.0001, dot(directionToProbe, normal));    //
            weight *= Square(max(0.0001, (dot(directionToProbe, normal) + 1.0) * 0.5)) + 0.2;  
        }

        //3. 切比雪夫系数
        if (ddgi.visibilityTest)
        {
            vec3 bias           = (normal + 3.0 * viewVec) * ddgi.normalBias;
            vec3 probeToPoint   = (worldPos - probePos) + bias;
            vec3 dir            = normalize(-probeToPoint);
            float dist          = length(probeToPoint);
            vec2 texCoord       = FetchProbeAbsTexCoord(-dir, probeIdx, ddgi.depthTextureWidth, ddgi.depthTextureHeight, DDGI_DEPTH_PROBE_SIZE);


            vec2 temp       = texture(sampler2D(depthTexture, sampl), texCoord).rg;  //采样距离和距离平方
            float mean      = temp.x;
            float variance  = abs(Square(temp.x) - temp.y);

            float chebyshev = variance / (variance + Square(max(dist - mean, 0.0)));
            chebyshev       = max(Pow3(chebyshev), 0.0);  //以切比雪夫系数三次方作为权重

            weight *= (dist <= mean) ? 1.0 : chebyshev;
        }

        //避免计算精度问题
        {
            weight = max(0.000001, weight); 

            const float crushThreshold = 0.2f;
            if (weight < crushThreshold)
                weight *= weight * weight * (1.0f / Square(crushThreshold)); 
        }


        //1. 三线性插值系数
        {
            vec3 trilinear = mix(1.0 - alpha, alpha, offset);
            weight *= trilinear.x * trilinear.y * trilinear.z;  
        }

        //采样，累计光照      
        {
            vec3 irradianceDir     = normal;
            vec2 texCoord          = FetchProbeAbsTexCoord(normalize(irradianceDir), probeIdx, ddgi.irradianceTextureWidth, ddgi.irradianceTextureHeight, DDGI_IRRADIANCE_PROBE_SIZE);
            vec3 probeIrradiance   = texture(sampler2D(irradianceTexture, sampl), texCoord).rgb;
        
            sumIrradiance += weight * probeIrradiance;
            sumWeight += weight;
        }
    }

    vec3 netIrradiance = sumIrradiance / sumWeight;
    netIrradiance *= ddgi.energyPreservation;

    return 2 * PI * netIrradiance;   
}

#endif