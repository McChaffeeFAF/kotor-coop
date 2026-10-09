#include "u_screeneffect.glsl"

// Temporal AA resolve. The scene is drawn with a sub-pixel projection jitter
// (Halton 2,3); this blends the frame with the previous result, reprojected
// by camera motion from the opaque depth (nearest depth of the 3x3, so edges
// reproject with their foreground), and clips the history to the current
// neighbourhood's colour box in YCoCg (variance clipping) so moving objects
// without motion vectors do not ghost.
// uTAAParams: x = history weight, y = 1 when the history is valid.

const float CLIP_GAMMA = 1.0;

uniform sampler2D sMainTex;   // current frame
uniform sampler2D sHilights;  // history
uniform sampler2D sGBufDepth; // opaque depth

noperspective in vec2 fragUV1;

out vec4 fragColor;

vec3 toYCoCg(vec3 c) {
    return vec3(
        0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
        0.5 * c.r - 0.5 * c.b,
        -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 fromYCoCg(vec3 c) {
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

vec3 clipToBox(vec3 history, vec3 lo, vec3 hi) {
    vec3 center = 0.5 * (hi + lo);
    vec3 extent = 0.5 * (hi - lo) + 1e-4;
    vec3 v = history - center;
    vec3 units = abs(v / extent);
    float m = max(units.x, max(units.y, units.z));
    return m > 1.0 ? center + v / m : history;
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(sMainTex, 0);
    vec4 current = texelFetch(sMainTex, p, 0);

    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    float closest = 1.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 q = clamp(p + ivec2(x, y), ivec2(0), size - 1);
            vec3 c = toYCoCg(texelFetch(sMainTex, q, 0).rgb);
            m1 += c;
            m2 += c * c;
            closest = min(closest, texelFetch(sGBufDepth, q, 0).r);
        }
    }
    if (uTAAParams.y < 0.5) {
        fragColor = current;
        return;
    }
    vec4 ndc = vec4(2.0 * vec3(fragUV1, closest) - 1.0, 1.0);
    vec4 prev = uReprojection * ndc;
    vec2 prevUV = 0.5 * prev.xy / prev.w + 0.5;
    if (any(lessThan(prevUV, vec2(0.0))) || any(greaterThan(prevUV, vec2(1.0)))) {
        fragColor = current;
        return;
    }
    vec3 mean = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    vec3 lo = mean - CLIP_GAMMA * sigma;
    vec3 hi = mean + CLIP_GAMMA * sigma;
    vec3 history = toYCoCg(texture(sHilights, prevUV).rgb);
    history = clipToBox(history, lo, hi);
    vec3 resolved = mix(toYCoCg(current.rgb), history, uTAAParams.x);
    fragColor = vec4(max(fromYCoCg(resolved), vec3(0.0)), current.a);
}
