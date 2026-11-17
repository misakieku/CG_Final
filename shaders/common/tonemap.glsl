#pragma once

const vec3 kNightSkyColor = vec3(0.012, 0.018, 0.036);

vec3 ACESFilm(vec3 x)
{
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 linearToSrgb(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
}
