#include "u_locals.glsl"

// Dialog and script video effect (videoeffects.2da) over the rendered scene.
// uColor.rgb: per-channel modulation. uAmbientColor: x = saturation amount
// (towards grey), y = saturation enabled, z = scan noise enabled, w = time.

uniform sampler2D sMainTex;

noperspective in vec2 fragUV1;

out vec4 fragColor;

float noise(vec2 cell, float frame) {
    return fract(sin(dot(cell + frame, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 uv = vec2(uUV * vec3(fragUV1, 1.0));
    vec4 scene = texture(sMainTex, uv);
    vec3 color = scene.rgb;
    if (uAmbientColor.y > 0.5) {
        // Each output channel is its modulation times the mix of luminance
        // and that channel by the saturation, with every weight capped at 1
        // except blue's own (the colour math of upstream #356's video effect).
        float s = uAmbientColor.x;
        vec3 gray = (1.0 - s) * vec3(0.3086, 0.6094, 0.0820);
        vec3 red = min(uColor.r * (gray + vec3(s, 0.0, 0.0)), vec3(1.0));
        vec3 green = min(uColor.g * (gray + vec3(0.0, s, 0.0)), vec3(1.0));
        vec3 blue = uColor.b * (gray + vec3(0.0, 0.0, s));
        blue.xy = min(blue.xy, vec2(1.0));
        color = vec3(dot(color, red), dot(color, green), dot(color, blue));
    }
    if (uAmbientColor.z > 0.5) {
        // Fine 32x32-texel grain tiled over the screen, re-randomised each frame.
        vec2 cell = floor(gl_FragCoord.xy / 2.0);
        float frame = floor(uAmbientColor.w * 30.0);
        color += vec3(noise(mod(cell, 32.0), frame) * 0.02);
    }
    fragColor = vec4(clamp(color, 0.0, 1.0), scene.a);
}
