#include "u_screeneffect.glsl"

// Bloom upsample: a 3x3 tent over the smaller level (sMainTex), added to
// this level's downsample (sHilights). uScreenResolutionRcp is the smaller
// level's texel size.

uniform sampler2D sMainTex;
uniform sampler2D sHilights;

noperspective in vec2 fragUV1;

out vec4 fragColor;

void main() {
    vec2 t = uScreenResolutionRcp;
    vec2 uv = fragUV1;
    vec3 sum = 4.0 * texture(sMainTex, uv).rgb;
    sum += 2.0 * (texture(sMainTex, uv + vec2(t.x, 0.0)).rgb +
                  texture(sMainTex, uv - vec2(t.x, 0.0)).rgb +
                  texture(sMainTex, uv + vec2(0.0, t.y)).rgb +
                  texture(sMainTex, uv - vec2(0.0, t.y)).rgb);
    sum += texture(sMainTex, uv + t).rgb +
           texture(sMainTex, uv - t).rgb +
           texture(sMainTex, uv + vec2(t.x, -t.y)).rgb +
           texture(sMainTex, uv + vec2(-t.x, t.y)).rgb;
    vec3 upsampled = sum / 16.0;
    fragColor = vec4(texture(sHilights, uv).rgb + upsampled, 1.0);
}
