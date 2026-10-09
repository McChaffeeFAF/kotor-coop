#include "u_screeneffect.glsl"

// Lens effects (design note graphics_preset), each its own toggle in
// uLensFlags: x lens ghosts and halo from bright parts of the frame, y film
// grain, z vignette, w chromatic aberration. uLensParams: x noise frame.

uniform sampler2D sMainTex;

noperspective in vec2 fragUV1;

out vec4 fragColor;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 bright(vec2 uv) {
    vec3 c = textureLod(sMainTex, uv, 0.0).rgb;
    float l = max(c.r, max(c.g, c.b));
    return c * smoothstep(0.85, 1.0, l);
}

void main() {
    vec2 uv = fragUV1;
    vec2 fromCentre = uv - 0.5;
    vec3 color;
    if (uLensFlags.w > 0.5) {
        // Chromatic aberration: red and blue split radially, most at the edges.
        vec2 shift = fromCentre * dot(fromCentre, fromCentre) * 0.012;
        color.r = texture(sMainTex, uv + shift).r;
        color.g = texture(sMainTex, uv).g;
        color.b = texture(sMainTex, uv - shift).b;
    } else {
        color = texture(sMainTex, uv).rgb;
    }
    if (uLensFlags.x > 0.5) {
        // Ghosts: bright spots mirrored through the centre at a few scales,
        // tinted per ghost, and a thin halo ring; faint by design.
        vec2 mirrored = 0.5 - fromCentre;
        vec3 ghosts = vec3(0.0);
        const vec3 TINTS[4] = vec3[](vec3(1.0, 0.75, 0.55), vec3(0.6, 0.85, 1.0), vec3(0.9, 0.6, 1.0), vec3(0.65, 1.0, 0.75));
        for (int i = 0; i < 4; ++i) {
            float scale = 0.35 + 0.3 * float(i);
            vec2 g = 0.5 + (mirrored - 0.5) * scale;
            float edge = 1.0 - smoothstep(0.3, 0.5, length(g - 0.5));
            ghosts += bright(g) * TINTS[i] * edge;
        }
        vec2 haloUV = 0.5 + normalize(fromCentre + 1e-5) * 0.42 * length(fromCentre * 2.0);
        ghosts += bright(mix(uv, haloUV, 0.5)) * vec3(0.8, 0.85, 1.0) * 0.5;
        color += ghosts * 0.06;
    }
    if (uLensFlags.z > 0.5) {
        // Vignette: a soft darkening of the corners only.
        float d = length(fromCentre * vec2(1.0, uScreenResolution.y / uScreenResolution.x) * 1.6);
        color *= mix(1.0, 0.78, smoothstep(0.55, 1.15, d));
    }
    if (uLensFlags.y > 0.5) {
        // Film grain: luma-weighted noise, strongest in the mid tones, new each frame.
        float n = hash12(gl_FragCoord.xy + 97.0 * uLensParams.x) - 0.5;
        float l = dot(color, vec3(0.2126, 0.7152, 0.0722));
        color += n * 0.045 * (1.0 - abs(l * 2.0 - 1.0) * 0.6);
    }
    fragColor = vec4(color, 1.0);
}
