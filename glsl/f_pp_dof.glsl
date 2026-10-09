#include "u_screeneffect.glsl"

// Conversation depth of field (design note graphics_preset): the speaker
// stays sharp and what lies well behind (and very close in front of) the
// focus distance blurs, by a circle of confusion from linear depth. A
// 24-tap two-ring disk gathers the blur; a sample only counts as far as its
// own blur reaches, so a sharp subject does not bleed onto the background.
// uDOFParams: focus distance (m), amount (0-1, the fade), max radius (px),
// in-focus depth (m) on each side of the focus.

uniform sampler2D sMainTex;
uniform sampler2D sGBufDepth;

noperspective in vec2 fragUV1;

out vec4 fragColor;

const int TAPS = 24;
const float GOLDEN = 2.39996323;

float linearDepth(float depth) {
    float z = depth * 2.0 - 1.0;
    return (2.0 * uClipNear * uClipFar) / (uClipFar + uClipNear - z * (uClipFar - uClipNear));
}

float coc(vec2 uv) {
    float z = linearDepth(texture(sGBufDepth, uv).r);
    float focus = uDOFParams.x;
    float range = uDOFParams.w;
    // Behind: ramps over the next few metres. In front: only very close.
    float behind = smoothstep(focus + range, focus + range + 1.5 + 0.25 * focus, z);
    float front = 1.0 - smoothstep(0.35 * focus, 0.7 * focus, z);
    return clamp(max(behind, 0.6 * front), 0.0, 1.0) * uDOFParams.y;
}

void main() {
    vec4 color = texture(sMainTex, fragUV1);
    float centerCoc = coc(fragUV1);
    if (uDOFParams.y <= 0.001) {
        fragColor = color;
        return;
    }
    float maxRadius = uDOFParams.z;
    vec3 sum = color.rgb;
    float weightSum = 1.0;
    for (int i = 0; i < TAPS; ++i) {
        float r = sqrt((float(i) + 0.5) / float(TAPS));
        float a = float(i) * GOLDEN;
        vec2 offset = vec2(cos(a), sin(a)) * r * maxRadius * uScreenResolutionRcp;
        vec2 suv = fragUV1 + offset * max(centerCoc, 0.0);
        float sampleCoc = coc(suv);
        // A sample spreads only as far as its own blur: it counts when its
        // circle reaches this pixel.
        float w = smoothstep(r - 0.15, r + 0.05, sampleCoc) * smoothstep(0.0, 0.1, centerCoc);
        sum += texture(sMainTex, suv).rgb * w;
        weightSum += w;
    }
    fragColor = vec4(sum / weightSum, color.a);
}
