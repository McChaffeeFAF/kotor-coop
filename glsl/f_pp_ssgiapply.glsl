#include "u_screeneffect.glsl"

// Screen-space GI, apply: the half-resolution bounce (sHilights) upsampled
// with depth-aware weights over 3x3 (which also smooths its noise) and added
// as light reflected by the surface: its own colour stands in for albedo.
// uGIParams.y is the intensity.

uniform sampler2D sMainTex;
uniform sampler2D sHilights;
uniform sampler2D sGBufDepth;

noperspective in vec2 fragUV1;

out vec4 fragColor;

float linearDepth(float depth) {
    float z = depth * 2.0 - 1.0;
    return (2.0 * uClipNear * uClipFar) / (uClipFar + uClipNear - z * (uClipFar - uClipNear));
}

void main() {
    vec4 color = texture(sMainTex, fragUV1);
    ivec2 fullSize = textureSize(sGBufDepth, 0);
    ivec2 halfSize = textureSize(sHilights, 0);
    float centerDepth = linearDepth(texelFetch(sGBufDepth, ivec2(gl_FragCoord.xy), 0).r);
    ivec2 halfCoord = ivec2(gl_FragCoord.xy) / 2;
    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 h = clamp(halfCoord + ivec2(x, y), ivec2(0), halfSize - 1);
            float d = linearDepth(texelFetch(sGBufDepth, min(h * 2, fullSize - 1), 0).r);
            float w = (x == 0 && y == 0 ? 2.0 : 1.0) / (1e-3 + abs(d - centerDepth) / max(centerDepth, 0.1));
            sum += w * texelFetch(sHilights, h, 0).rgb;
            weightSum += w;
        }
    }
    vec3 bounce = sum / max(weightSum, 1e-4);
    vec3 albedo = clamp(color.rgb * 1.2 + 0.05, 0.0, 1.0);
    fragColor = vec4(color.rgb + uGIParams.y * bounce * albedo, color.a);
}
