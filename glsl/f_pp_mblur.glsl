#include "u_screeneffect.glsl"

// Camera motion blur (design note graphics_preset): each pixel's position
// last frame comes from its depth and uReprojection (this frame's clip
// space to the previous frame's, unjittered); the colour is averaged along
// that screen-space motion, centred on the pixel and clamped in length.
// Samples much nearer the camera than the centre are skipped, so a still
// foreground does not smear over a moving background edge.
// uMotionParams: strength (shutter fraction), max length (px), history valid.

uniform sampler2D sMainTex;
uniform sampler2D sGBufDepth;

noperspective in vec2 fragUV1;

out vec4 fragColor;

const int SAMPLES = 12;

void main() {
    vec4 color = texture(sMainTex, fragUV1);
    if (uMotionParams.z < 0.5) {
        fragColor = color;
        return;
    }
    float depth = texture(sGBufDepth, fragUV1).r;
    vec4 clip = vec4(fragUV1 * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 previous = uReprojection * clip;
    vec2 previousUV = previous.xy / previous.w * 0.5 + 0.5;
    vec2 motion = (fragUV1 - previousUV) * uMotionParams.x;
    vec2 motionPx = motion * uScreenResolution;
    float lengthPx = length(motionPx);
    if (lengthPx < 0.5) {
        fragColor = color;
        return;
    }
    if (lengthPx > uMotionParams.y) {
        motion *= uMotionParams.y / lengthPx;
    }
    vec3 sum = color.rgb;
    float weightSum = 1.0;
    for (int i = 0; i < SAMPLES; ++i) {
        float t = (float(i) + 0.5) / float(SAMPLES) - 0.5;
        vec2 uv = fragUV1 + motion * t;
        float sampleDepth = texture(sGBufDepth, uv).r;
        float w = sampleDepth + 0.002 < depth ? 0.0 : 1.0;
        sum += texture(sMainTex, uv).rgb * w;
        weightSum += w;
    }
    fragColor = vec4(sum / weightSum, color.a);
}
