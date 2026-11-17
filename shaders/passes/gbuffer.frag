#version 460 core
in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec2 vUV;
flat in uint vMaterialId;
flat in uint vFlags;

#include "common/frame_ubo.glsl"
#include "common/scene_types.glsl"

layout(binding = 0) uniform sampler2DArray uAlbedoArray;
layout(binding = 1) uniform sampler2DArray uOrmArray;
layout(binding = 2) uniform sampler2DArray uNormalArray;

layout(location = 0) out vec4 outGBuffer0; // Albedo.rgb + AO
layout(location = 1) out vec4 outGBuffer1; // WorldNormal.xyz + Roughness

vec2 computeWorldBoxUV(vec3 worldPos, vec3 worldNormal)
{
    vec3 an = abs(worldNormal);
    vec2 uv;
    if (an.x >= an.y && an.x >= an.z)
    {
        uv = worldPos.zy;
    }
    else if (an.y >= an.x && an.y >= an.z)
    {
        uv = worldPos.xz;
    }
    else
    {
        uv = worldPos.xy;
    }
    return uv * 0.333333; // 1 texture tile per 3.0m maze cell
}

mat3 computeCotangentTbn(vec3 N, vec3 worldPos, vec2 duv1, vec2 duv2, out bool valid)
{
    // Scale differentials before cross products to preserve float32 precision at close range
    vec3 dp1 = dFdx(worldPos) * 1024.0;
    vec3 dp2 = dFdy(worldPos) * 1024.0;
    vec2 sduv1 = duv1 * 1024.0;
    vec2 sduv2 = duv2 * 1024.0;

    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * sduv1.x + dp1perp * sduv2.x;
    vec3 B = dp2perp * sduv1.y + dp1perp * sduv2.y;

    float maxSq = max(dot(T, T), dot(B, B));
    if (maxSq <= 1e-24)
    {
        valid = false;
        return mat3(1.0);
    }
    valid = true;
    float invmax = inversesqrt(maxSq);
    return mat3(T * invmax, B * invmax, N);
}

vec2 parallaxOcclusionMapping(vec2 uv, vec3 viewDirTS, float layer, vec2 duvdx, vec2 duvdy, float heightScale)
{
    float numLayers = mix(16.0, 6.0, clamp(abs(viewDirTS.z), 0.0, 1.0));
    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;

    vec2 maxDelta = (viewDirTS.xy / max(abs(viewDirTS.z), 0.24)) * heightScale;
    vec2 deltaUV = maxDelta / numLayers;

    vec2 currentUV = uv;
    float currentDepthMap = 1.0 - textureGrad(uNormalArray, vec3(currentUV, layer), duvdx, duvdy).a;

    for (int step = 0; step < 18 && currentLayerDepth < currentDepthMap; ++step)
    {
        currentUV -= deltaUV;
        currentDepthMap = 1.0 - textureGrad(uNormalArray, vec3(currentUV, layer), duvdx, duvdy).a;
        currentLayerDepth += layerDepth;
    }

    vec2 prevUV = currentUV + deltaUV;
    float afterDepth = currentDepthMap - currentLayerDepth;
    float beforeDepth = (1.0 - textureGrad(uNormalArray, vec3(prevUV, layer), duvdx, duvdy).a) - (currentLayerDepth - layerDepth);
    float denom = afterDepth - beforeDepth;
    float weight = (abs(denom) > 1e-5) ? clamp(afterDepth / denom, 0.0, 1.0) : 0.0;
    return mix(currentUV, prevUV, weight);
}

void main()
{
    vec3 geomN = normalize(vWorldNormal);
    vec2 baseUV = ((vFlags & kObjectWorldUVFlag) != 0u) ? computeWorldBoxUV(vWorldPos, geomN) : vUV;
    vec2 duvdx = dFdx(baseUV);
    vec2 duvdy = dFdy(baseUV);

    bool tbnValid = false;
    mat3 TBN = computeCotangentTbn(geomN, vWorldPos, duvdx, duvdy, tbnValid);

    vec2 sampleUV = baseUV;
    if (tbnValid && vMaterialId == 0u && (vFlags & kObjectWorldUVFlag) != 0u)
    {
        float camDist = length(uCameraPos.xyz - vWorldPos);
        float distFade = 1.0 - smoothstep(8.0, 15.0, camDist);
        if (distFade > 0.01)
        {
            vec3 viewDirWS = normalize(uCameraPos.xyz - vWorldPos);
            vec3 viewDirTS = transpose(TBN) * viewDirWS;
            if (viewDirTS.z > 0.01)
            {
                sampleUV = parallaxOcclusionMapping(baseUV, viewDirTS, float(vMaterialId), duvdx, duvdy, 0.022 * distFade);
            }
        }
    }

    vec3 arrayCoord = vec3(sampleUV, float(vMaterialId));
    vec4 albedoSample = textureGrad(uAlbedoArray, arrayCoord, duvdx, duvdy);
    vec4 ormSample = textureGrad(uOrmArray, arrayCoord, duvdx, duvdy);
    vec3 tangentN = textureGrad(uNormalArray, arrayCoord, duvdx, duvdy).xyz * 2.0 - 1.0;
    vec3 worldN = tbnValid ? normalize(TBN * tangentN) : geomN;

    outGBuffer0 = vec4(albedoSample.rgb, ormSample.r);
    outGBuffer1 = vec4(worldN, ormSample.g);
}
