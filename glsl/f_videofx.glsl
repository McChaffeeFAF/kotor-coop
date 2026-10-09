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
        float s = uAmbientColor.x;
        vec3 w = vec3(0.3086, 0.6094, 0.0820);
        mat3 m = mat3(
            (1.0 - s) * w.r + s, (1.0 - s) * w.r, (1.0 - s) * w.r,
            (1.0 - s) * w.g, (1.0 - s) * w.g + s, (1.0 - s) * w.g,
            (1.0 - s) * w.b, (1.0 - s) * w.b, (1.0 - s) * w.b + s);
        color = uColor.rgb * clamp(m * color, 0.0, 1.0);
    }
    if (uAmbientColor.z > 0.5) {
        // Fine 32x32-texel grain tiled over the screen, re-randomised each frame.
        vec2 cell = floor(gl_FragCoord.xy / 2.0);
        float frame = floor(uAmbientColor.w * 30.0);
        color += vec3(noise(mod(cell, 32.0), frame) * 0.02);
    }
    fragColor = vec4(clamp(color, 0.0, 1.0), scene.a);
}
