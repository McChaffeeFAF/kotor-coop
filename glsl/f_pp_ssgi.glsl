#include "u_screeneffect.glsl"

#include "i_coords.glsl"
#include "i_math.glsl"

// Screen-space GI, one bounce, at half resolution: around each pixel,
// SAMPLES points of the opaque frame within uGIParams.x metres (a Vogel
// disk projected to the screen, rotated per pixel and frame) light it by
// their colour, weighted by the cosine to the depth-derived normal and a
// distance falloff. Out: rgb the gathered bounce light.
// uGIParams: x radius (m), y intensity (applied later), z noise frame.
// uScreenResolution is the full resolution.

const int SAMPLES = 12;
const float GOLDEN_ANGLE_GI = 2.39996323;
const float MAX_PIXELS = 200.0;

uniform sampler2D sMainTex;
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
    vec3 dx = abs(r.z - center.z) < abs(center.z - l.z) ? r - center : center - l;
    vec3 dy = abs(u.z - center.z) < abs(center.z - b.z) ? u - center : center - b;
    return normalize(cross(dx, dy));
}

float noise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec2 uv = (floor(gl_FragCoord.xy) * 2.0 + 1.0) * uScreenResolutionRcp;
    float depth = texture(sGBufDepth, uv).r;
    if (depth >= 1.0) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec3 P = viewPosAt(uv);
    vec3 N = viewNormal(uv, P);
    float radius = uGIParams.x;
    // Metres to pixels at this depth.
    float pixelsPerMetre = 0.5 * uScreenResolution.y * uProjection[1][1] / max(-P.z, 0.1);
    float radiusPixels = min(radius * pixelsPerMetre, MAX_PIXELS);
    if (radiusPixels < 2.0) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    float rotation = 6.2831853 * noise(gl_FragCoord.xy + 7.13 * uGIParams.z);
    vec3 gathered = vec3(0.0);
    float weightSum = 0.0;
    for (int i = 0; i < SAMPLES; ++i) {
        float r = sqrt((float(i) + 0.5) / float(SAMPLES)) * radiusPixels;
        float a = float(i) * GOLDEN_ANGLE_GI + rotation;
        vec2 suv = uv + vec2(cos(a), sin(a)) * r * uScreenResolutionRcp;
        if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) {
            continue;
        }
        vec3 Q = viewPosAt(suv);
        vec3 v = Q - P;
        float d = length(v);
        float cosine = max(0.0, dot(N, v / max(d, 1e-4)));
        float falloff = 1.0 - smoothstep(0.5 * radius, 1.5 * radius, d);
        float w = cosine * falloff;
        gathered += w * textureLod(sMainTex, suv, 0.0).rgb;
        weightSum += 1.0;
    }
    fragColor = vec4(gathered / max(weightSum, 1.0), 1.0);
}
