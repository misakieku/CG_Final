#version 460 core
layout(early_fragment_tests) in;

in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec2 vUV;
flat in uint vMaterialId;
flat in uint vFlags;
flat in uint vObjectId;

#include "common/frame_ubo.glsl"
#include "common/scene_types.glsl"
#include "common/gbuffer_common.glsl"

layout(binding = 0) uniform sampler2DArray uAlbedoArray;
layout(binding = 1) uniform sampler2DArray uOrmArray;
layout(binding = 2) uniform sampler2DArray uNormalArray;

layout(location = 0) out vec4 outGBuffer0; // Albedo.rgb + AO
layout(location = 1) out vec4 outGBuffer1; // OctNormal.xy + MatId + SignedRoughness

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

    if (uGiMisc.w > 0.0)
    {
        outGBuffer0 = vec4(vec3(uGiMisc.w), 1.0);
        outGBuffer1 = vec4(octEncodeNormal(geomN), float(vMaterialId), 0.78);
        return;
    }

    vec2 baseUV = ((vFlags & kObjectWorldUVFlag) != 0u) ? computeWorldBoxUV(vWorldPos, geomN) : vUV;
    // Compute derivatives before per-brick UV offset so 2x2 quad edges between bricks never spike TBN
    vec2 duvdx = dFdx(baseUV);
    vec2 duvdy = dFdy(baseUV);

    bool hasStoneDetail = (vFlags & kObjectStoneDetailFlag) != 0u;
    float brickId = 0.5;
    float brickEdge = 0.66;
    if (hasStoneDetail)
    {
        float objSalt = fract(sin(float(vObjectId) * 12.9898 + 1.37) * 43758.5453);
        brickId = fract(vUV.x + objSalt);
        brickEdge = clamp(vUV.y, 0.0, 1.0);
        baseUV += vec2(brickId * 0.73, fract(brickId * 1.6180339) * 0.59);
    }

    bool tbnValid = false;
    mat3 TBN = computeCotangentTbn(geomN, vWorldPos, duvdx, duvdy, tbnValid);

    vec3 arrayCoord = vec3(baseUV, float(vMaterialId));
    vec4 albedoSample = textureGrad(uAlbedoArray, arrayCoord, duvdx, duvdy);
    vec4 ormSample = textureGrad(uOrmArray, arrayCoord, duvdx, duvdy);
    vec3 tangentN = textureGrad(uNormalArray, arrayCoord, duvdx, duvdy).xyz * 2.0 - 1.0;
    vec3 worldN = tbnValid ? normalize(TBN * tangentN) : geomN;

    vec3 albedo = albedoSample.rgb;
    float ao = ormSample.r;
    float roughness = clamp(ormSample.g, 0.04, 1.0);

    // Stylized per-brick slate color variation, crevice AO, and chipped bevel edge-wear
    if (hasStoneDetail)
    {
        float toneBand = fract(brickId * 3.17);
        vec3 darkSlateTint = vec3(0.66, 0.68, 0.74);
        vec3 midSlateTint = vec3(0.92, 0.93, 0.96);
        vec3 lightSlateTint = vec3(1.04, 1.04, 1.05);
        vec3 warmSlateTint = vec3(0.98, 0.93, 0.88);

        vec3 brickTint = (toneBand < 0.28)
            ? mix(darkSlateTint, midSlateTint, smoothstep(0.0, 0.28, toneBand))
            : ((toneBand < 0.78)
                ? mix(midSlateTint, lightSlateTint, (toneBand - 0.28) / 0.50)
                : mix(lightSlateTint, warmSlateTint, (toneBand - 0.78) / 0.22));
        albedo *= brickTint;
        roughness = clamp(roughness + (brickId - 0.5) * 0.10, 0.35, 0.95);

        float creviceFactor = 1.0 - smoothstep(0.08, 0.42, brickEdge);
        ao *= mix(1.0, 0.42, creviceFactor);
        albedo *= mix(vec3(1.0), vec3(0.56, 0.58, 0.64), creviceFactor);

        float bevelWear = smoothstep(0.76, 0.98, brickEdge);
        vec3 wearHighlight = max(albedo * 1.24, vec3(0.48, 0.50, 0.53));
        albedo = mix(albedo, wearHighlight, bevelWear * 0.24);
        roughness = clamp(roughness - bevelWear * 0.04, 0.45, 0.95);
    }

    // Subtle damp floor-skirt transition near ground without darkening floor GI
    if (vMaterialId <= 1u)
    {
        float macroA = sin(vWorldPos.x * 0.43 + vWorldPos.z * 0.37) * cos(vWorldPos.z * 0.51 - vWorldPos.y * 0.63);
        float dampHeight = 1.0 - smoothstep(0.0, 0.45, max(vWorldPos.y, 0.0));
        float dampPatch = clamp(0.45 + 0.55 * macroA, 0.0, 1.0);
        float dampMask = dampHeight * dampPatch;
        albedo = mix(albedo, albedo * vec3(0.76, 0.80, 0.75), dampMask * 0.32);
        roughness = mix(roughness, max(0.36, roughness * 0.68), dampMask * 0.60);
    }

    // GBuffer1 stores OctNormal.xy, vMaterialId in B, and signed roughness in A (negative = metal)
    float encodedRoughness = (ormSample.b > 0.5) ? -roughness : roughness;
    outGBuffer0 = vec4(albedo, ao);
    outGBuffer1 = vec4(octEncodeNormal(worldN), float(vMaterialId), encodedRoughness);
}
