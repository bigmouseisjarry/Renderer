#ifndef SKY_GLSL
#define SKY_GLSL

//默认纹理////////////////////////////////////////////////////////////////////////////

int SkyboxTextureSize() {
    Material material = MATERIALS.slot[GLOBAL_SETTING.skyboxMaterialID];

    return textureSize(TEXTURES_CUBE[material.textureSlotsCube[0]], 0).s;
}

vec3 FetchSkyLight(vec3 sampleVec, float lod) {
    Material material = MATERIALS.slot[GLOBAL_SETTING.skyboxMaterialID];

    vec3 sky        = FetchMaterialTexCube(material, 0, sampleVec, lod).xyz;
    float intencity = FetchMaterialFloat(material, 0);
    // float intencity = 1.0f;

    vec3 skyLight   = pow(sky, vec3(1.0/2.2)) * intencity;
    return skyLight;
}

vec3 FetchSkyLight(vec3 sampleVec) {
    return FetchSkyLight(sampleVec, 0.0f);
}

#endif
