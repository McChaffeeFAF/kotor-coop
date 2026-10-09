#include "u_screeneffect.glsl"

// Bloom, first step: the bright part of the composited frame plus the glow
// channel (the blurred hilights buffer and the additive particle glow that
// f_oit_particles keeps in the revealage target), at half resolution.

uniform sampler2D sMainTex;
uniform sampler2D sHilights;
uniform sampler2D sOITRevealage;

noperspective in vec2 fragUV1;

out vec4 fragColor;

vec3 brightPart(vec3 color) {
    float threshold = uBloomParams.x;
    float knee = max(uBloomParams.y, 1e-4);
    float brightness = max(color.r, max(color.g, color.b));
    // Soft knee: a quadratic ramp from threshold - knee to threshold + knee.
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    float weight = max(soft, brightness - threshold) / max(brightness, 1e-4);
    return color * weight;
}

vec3 glowChannel(vec2 uv) {
    vec3 hilights = texture(sHilights, uv).rgb;
    vec4 rev = texture(sOITRevealage, uv);
    vec3 glow4 = vec3(rev.g, rev.b, -log(max(rev.a, 1e-6)));
    vec3 particles = pow(max(glow4, vec3(0.0)), vec3(0.25));
    return hilights + particles;
}

void main() {
    // Four bilinear taps cover the 4x4 source block under this half-res texel.
    vec2 o = 0.5 * uScreenResolutionRcp;
    vec3 color = vec3(0.0);
    vec3 glow = vec3(0.0);
    vec2 offsets[4] = vec2[](vec2(-o.x, -o.y), vec2(o.x, -o.y), vec2(-o.x, o.y), vec2(o.x, o.y));
    for (int i = 0; i < 4; ++i) {
        vec2 uv = fragUV1 + offsets[i];
        vec3 c = brightPart(texture(sMainTex, uv).rgb);
        // Karis average: a lone bright texel does not flicker as a big blob.
        c /= 1.0 + max(c.r, max(c.g, c.b));
        color += c;
        glow += glowChannel(uv);
    }
    color *= 0.25;
    color /= max(1.0 - max(color.r, max(color.g, color.b)), 1e-3);
    fragColor = vec4(color + uBloomParams.w * 0.25 * glow, 1.0);
}
