#pragma once

struct GrassInstance
{
    vec4 posYaw;    // xyz = world root, w = azimuth yaw (rad)
    vec4 scaleSeed; // x = widthScale, y = heightScale, z = colorSeed [0,1], w = leanScale
};

float grassCullRadius(GrassInstance inst)
{
    return 0.34 * max(inst.scaleSeed.x, inst.scaleSeed.y);
}

float evalGrassScreenPct(GrassInstance inst, out float distToCam, out float radius)
{
    radius = grassCullRadius(inst);
    distToCam = length(inst.posYaw.xyz - uCameraPos.xyz);
    return (radius * abs(uProj[1][1])) / max(distToCam, 0.01);
}
