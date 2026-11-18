#version 460 core
in vec2 vUV;

#include "common/frame_ubo.glsl"

layout(binding = 0) uniform sampler2D uLdrTex; // RGB = sRGB, A = perceived luma

out vec4 FragColor;

// FXAA 3.11 quality parameters tuned for stylized stone edges & fine foliage
const float kEdgeThreshold = 0.125;
const float kEdgeThresholdMin = 0.0312;
const float kSubpixQuality = 0.75;
const int kSearchSteps = 12;

float sampleLuma(vec2 uv)
{
    return texture(uLdrTex, uv).a;
}

float searchStepDist(int idx)
{
    if (idx < 5)
    {
        return 1.0;
    }
    if (idx < 9)
    {
        return 2.0;
    }
    return 4.0;
}

void main()
{
    vec2 invRes = uScreenParams.zw;
    vec4 centerTap = texture(uLdrTex, vUV);
    float lumaC = centerTap.a;

    float lumaD = sampleLuma(vUV + vec2(0.0, -invRes.y));
    float lumaU = sampleLuma(vUV + vec2(0.0, invRes.y));
    float lumaL = sampleLuma(vUV + vec2(-invRes.x, 0.0));
    float lumaR = sampleLuma(vUV + vec2(invRes.x, 0.0));

    float lumaMin = min(lumaC, min(min(lumaD, lumaU), min(lumaL, lumaR)));
    float lumaMax = max(lumaC, max(max(lumaD, lumaU), max(lumaL, lumaR)));
    float lumaRange = lumaMax - lumaMin;

    if (lumaRange < max(kEdgeThresholdMin, lumaMax * kEdgeThreshold))
    {
        FragColor = vec4(centerTap.rgb, 1.0);
        return;
    }

    float lumaDL = sampleLuma(vUV + vec2(-invRes.x, -invRes.y));
    float lumaUR = sampleLuma(vUV + vec2(invRes.x, invRes.y));
    float lumaUL = sampleLuma(vUV + vec2(-invRes.x, invRes.y));
    float lumaDR = sampleLuma(vUV + vec2(invRes.x, -invRes.y));

    float lumaDU = lumaD + lumaU;
    float lumaLR = lumaL + lumaR;
    float lumaLeftCorners = lumaDL + lumaUL;
    float lumaRightCorners = lumaDR + lumaUR;
    float lumaDownCorners = lumaDL + lumaDR;
    float lumaUpCorners = lumaUL + lumaUR;

    float edgeHorz = abs(-2.0 * lumaL + lumaLeftCorners) + abs(-2.0 * lumaC + lumaDU) * 2.0 + abs(-2.0 * lumaR + lumaRightCorners);
    float edgeVert = abs(-2.0 * lumaU + lumaUpCorners) + abs(-2.0 * lumaC + lumaLR) * 2.0 + abs(-2.0 * lumaD + lumaDownCorners);
    bool isHorizontal = (edgeHorz >= edgeVert);

    float stepLength = isHorizontal ? invRes.y : invRes.x;
    float luma1 = isHorizontal ? lumaD : lumaL;
    float luma2 = isHorizontal ? lumaU : lumaR;
    float grad1 = luma1 - lumaC;
    float grad2 = luma2 - lumaC;
    bool is1Steepest = abs(grad1) >= abs(grad2);

    float gradScaled = 0.25 * max(abs(grad1), abs(grad2));
    float lumaLocalAvg = is1Steepest ? (0.5 * (luma1 + lumaC)) : (0.5 * (luma2 + lumaC));
    if (is1Steepest)
    {
        stepLength = -stepLength;
    }

    vec2 curUV = vUV;
    if (isHorizontal)
    {
        curUV.y += stepLength * 0.5;
    }
    else
    {
        curUV.x += stepLength * 0.5;
    }

    vec2 offset = isHorizontal ? vec2(invRes.x, 0.0) : vec2(0.0, invRes.y);
    vec2 uv1 = curUV - offset;
    vec2 uv2 = curUV + offset;

    float lumaEnd1 = sampleLuma(uv1) - lumaLocalAvg;
    float lumaEnd2 = sampleLuma(uv2) - lumaLocalAvg;
    bool reached1 = abs(lumaEnd1) >= gradScaled;
    bool reached2 = abs(lumaEnd2) >= gradScaled;

    for (int i = 1; i < kSearchSteps && !(reached1 && reached2); ++i)
    {
        float mul = searchStepDist(i);
        if (!reached1)
        {
            uv1 -= offset * mul;
            lumaEnd1 = sampleLuma(uv1) - lumaLocalAvg;
            reached1 = abs(lumaEnd1) >= gradScaled;
        }
        if (!reached2)
        {
            uv2 += offset * mul;
            lumaEnd2 = sampleLuma(uv2) - lumaLocalAvg;
            reached2 = abs(lumaEnd2) >= gradScaled;
        }
    }

    float dist1 = isHorizontal ? (vUV.x - uv1.x) : (vUV.y - uv1.y);
    float dist2 = isHorizontal ? (uv2.x - vUV.x) : (uv2.y - vUV.y);
    bool isDir1 = dist1 < dist2;
    float distFinal = min(dist1, dist2);
    float edgeLength = dist1 + dist2;
    float pixelOffset = -distFinal / max(edgeLength, 1e-5) + 0.5;

    bool isLumaCenterSmaller = lumaC < lumaLocalAvg;
    bool correctVariation = ((isDir1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    float lumaAvgFull = (1.0 / 12.0) * (2.0 * (lumaDU + lumaLR) + lumaLeftCorners + lumaRightCorners);
    float subpixOffset1 = clamp(abs(lumaAvgFull - lumaC) / lumaRange, 0.0, 1.0);
    float subpixOffset2 = (-2.0 * subpixOffset1 + 3.0) * subpixOffset1 * subpixOffset1;
    float subpixOffsetFinal = subpixOffset2 * subpixOffset2 * kSubpixQuality;

    finalOffset = max(finalOffset, subpixOffsetFinal);

    vec2 finalUV = vUV;
    if (isHorizontal)
    {
        finalUV.y += finalOffset * stepLength;
    }
    else
    {
        finalUV.x += finalOffset * stepLength;
    }

    FragColor = vec4(texture(uLdrTex, finalUV).rgb, 1.0);
}
