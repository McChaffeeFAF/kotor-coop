#include "u_screeneffect.glsl"

#include "i_coords.glsl"
#include "i_math.glsl"

// Ground-truth ambient occlusion (Jimenez et al., "Practical Real-Time
// Strategies for Accurate Indirect Occlusion", 2016): per pixel, SLICES
// screen-space slices; along each, the horizon angle on both sides is found
// by marching the depth buffer, and the visible arc between the horizons,
// cosine-weighted about the normal projected into the slice, is integrated
// analytically. Normals come from the depth buffer. The slice rotation and
// step jitter follow a 4x4 pattern that the blur pass averages out.
// uAOParams: x = radius (m), y = falloff (fraction of radius), z = noise frame.

const int SLICES = 2;
const int STEPS = 6;
const float MAX_PIXELS = 160.0;

uniform sampler2D sGBufDepth;

out vec4 fragColor;

vec3 viewPosAt(vec2 uv) {
    return reconstructViewPos(uv, sGBufDepth);
}

vec3 viewNormal(vec2 uv, vec3 center) {
    vec2 t = uScreenResolutionRcp;
    vec3 l = viewPosAt(uv - vec2(t.x, 0.0));
    vec3 r = viewPosAt(uv + vec2(t.x, 0.0));
    vec3 b = viewPosAt(uv - vec2(0.0, t.y));
    vec3 u = viewPosAt(uv + vec2(0.0, t.y));
    // Of each pair, the neighbour on the same surface (smaller depth step).
    vec3 dx = abs(r.z - center.z) < abs(center.z - l.z) ? r - center : center - l;
    vec3 dy = abs(u.z - center.z) < abs(center.z - b.z) ? u - center : center - b;
    return normalize(cross(dx, dy));
}

float fastAcos(float x) {
    float ax = abs(x);
    float r = (-0.156583 * ax + PI * 0.5) * sqrt(max(0.0, 1.0 - ax));
    return x >= 0.0 ? r : PI - r;
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec2 uv = (vec2(pixel) + 0.5) * uScreenResolutionRcp;
    float depth = texelFetch(sGBufDepth, pixel, 0).r;
    if (depth >= 1.0) {
        fragColor = vec4(1.0);
        return;
    }
    vec3 pos = viewPosAt(uv);
    vec3 normal = viewNormal(uv, pos);
    vec3 viewDir = normalize(-pos);
    if (dot(normal, viewDir) < 0.0) {
        normal = -normal;
    }

    float radius = uAOParams.x;
    float falloffRange = max(uAOParams.y * radius, 1e-3);
    float falloffFrom = radius - falloffRange;
    // Pixels covered by the radius at this depth.
    float pixelRadius = min(MAX_PIXELS, radius * uProjection[1][1] * 0.5 * uScreenResolution.y / max(-pos.z, 1e-3));
    if (pixelRadius < 1.5) {
        fragColor = vec4(1.0);
        return;
    }

    // 4x4 pattern: slice rotation and step offset, shifted per frame under TAA.
    int index = (pixel.x & 3) + 4 * (pixel.y & 3);
    float rotationNoise = fract(float(index) * 0.0625 + 0.03125 + uAOParams.z * 0.618034);
    float stepNoise = fract(float((index * 7) & 15) * 0.0625 + 0.03125 + uAOParams.z * 0.7548777);

    float visibility = 0.0;
    for (int slice = 0; slice < SLICES; ++slice) {
        float phi = (float(slice) + rotationNoise) * PI / float(SLICES);
        vec2 omega = vec2(cos(phi), sin(phi));
        vec3 direction = vec3(omega, 0.0);
        vec3 orthoDirection = direction - dot(direction, viewDir) * viewDir;
        vec3 axis = normalize(cross(orthoDirection, viewDir));
        vec3 projectedNormal = normal - axis * dot(normal, axis);
        float projectedLength = length(projectedNormal);
        if (projectedLength < 1e-4) {
            continue;
        }
        float signNormal = sign(dot(orthoDirection, projectedNormal));
        float cosNormal = clamp(dot(projectedNormal, viewDir) / projectedLength, -1.0, 1.0);
        float n = signNormal * fastAcos(cosNormal);

        float lowCos0 = cos(n + PI * 0.5);
        float lowCos1 = cos(n - PI * 0.5);
        float horizonCos0 = lowCos0;
        float horizonCos1 = lowCos1;
        for (int s = 0; s < STEPS; ++s) {
            float t = (float(s) + stepNoise) / float(STEPS);
            t = t * t;
            vec2 offset = max(t * pixelRadius, float(s) + 1.0) * omega * uScreenResolutionRcp;

            vec3 delta0 = viewPosAt(uv + offset) - pos;
            vec3 delta1 = viewPosAt(uv - offset) - pos;
            float len0 = length(delta0);
            float len1 = length(delta1);
            float weight0 = clamp((radius - len0) / falloffRange, 0.0, 1.0);
            float weight1 = clamp((radius - len1) / falloffRange, 0.0, 1.0);
            if (falloffFrom <= 0.0) {
                weight0 = len0 < radius ? weight0 : 0.0;
                weight1 = len1 < radius ? weight1 : 0.0;
            }
            float shc0 = mix(lowCos0, dot(delta0 / max(len0, 1e-4), viewDir), weight0);
            float shc1 = mix(lowCos1, dot(delta1 / max(len1, 1e-4), viewDir), weight1);
            horizonCos0 = max(horizonCos0, shc0);
            horizonCos1 = max(horizonCos1, shc1);
        }
        float h0 = n + clamp(-fastAcos(horizonCos1) - n, -PI * 0.5, PI * 0.5);
        float h1 = n + clamp(fastAcos(horizonCos0) - n, -PI * 0.5, PI * 0.5);
        float sinN = sin(n);
        float arc0 = (cosNormal + 2.0 * h0 * sinN - cos(2.0 * h0 - n)) * 0.25;
        float arc1 = (cosNormal + 2.0 * h1 * sinN - cos(2.0 * h1 - n)) * 0.25;
        visibility += projectedLength * (arc0 + arc1);
    }
    visibility = clamp(visibility / float(SLICES), 0.0, 1.0);
    // Fade out with distance, where the depth buffer is too coarse.
    visibility = mix(visibility, 1.0, smoothstep(40.0, 80.0, -pos.z));
    fragColor = vec4(visibility, -pos.z, 0.0, 1.0);
}
