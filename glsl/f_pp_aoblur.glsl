#include "u_screeneffect.glsl"

// Depth-aware separable blur of the AO term (r) using the linear depth kept
// in g; along uBlurDirection. Nine taps span the 4x4 noise pattern twice.

uniform sampler2D sMainTex;

out vec4 fragColor;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(sMainTex, 0);
    ivec2 dir = ivec2(uBlurDirection);
    vec2 center = texelFetch(sMainTex, p, 0).rg;
    float sum = 0.0;
    float weights = 0.0;
    for (int i = -4; i <= 4; ++i) {
        vec2 s = texelFetch(sMainTex, clamp(p + i * dir, ivec2(0), size - 1), 0).rg;
        float spatial = exp(-float(i * i) / 18.0);
        // Samples more than ~5% away in depth belong to another surface.
        float range = max(center.g * 0.05, 0.05);
        float similarity = max(0.0, 1.0 - abs(s.g - center.g) / range);
        float w = spatial * similarity;
        sum += s.r * w;
        weights += w;
    }
    fragColor = vec4(sum / max(weights, 1e-4), center.g, 0.0, 1.0);
}
