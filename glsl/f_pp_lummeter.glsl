#include "u_screeneffect.glsl"
#include "i_luma.glsl"

// Auto exposure, step 1: each texel of a small target meters one tile of
// the frame (4x4 bilinear taps): r is the weighted sum of log2 luma, g the
// weight. Tiles near the centre of the screen count more.

uniform sampler2D sMainTex;

out vec4 fragColor;

void main() {
    vec2 tiles = uExposureRange.zw; // the meter size
    vec2 tile = gl_FragCoord.xy - 0.5;
    float sumLog = 0.0;
    float sumWeight = 0.0;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            vec2 uv = (tile + (vec2(x, y) + 0.5) / 4.0) / tiles;
            vec3 color = textureLod(sMainTex, uv, 0.0).rgb;
            float luma = rgbToLuma(pow(max(color, vec3(0.0)), vec3(2.2))); // linear
            vec2 d = (uv - 0.5) * 2.0;
            float weight = 1.0 - 0.75 * clamp(dot(d, d) * 0.5, 0.0, 1.0);
            sumLog += weight * log2(luma + 1e-3);
            sumWeight += weight;
        }
    }
    fragColor = vec4(sumLog, sumWeight, 0.0, 1.0);
}
