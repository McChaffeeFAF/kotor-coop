#include "u_screeneffect.glsl"

// Bloom downsample: the 13-tap filter of Jimenez, "Next Generation Post
// Processing in Call of Duty: Advanced Warfare" (SIGGRAPH 2014): five
// overlapping 2x2 boxes, the centre one weighted 0.5 and the corners 0.125.
// uScreenResolutionRcp is the source texel size.

uniform sampler2D sMainTex;

noperspective in vec2 fragUV1;

out vec4 fragColor;

void main() {
    vec2 t = uScreenResolutionRcp;
    vec2 uv = fragUV1;
    vec3 a = texture(sMainTex, uv + t * vec2(-2.0, 2.0)).rgb;
    vec3 b = texture(sMainTex, uv + t * vec2(0.0, 2.0)).rgb;
    vec3 c = texture(sMainTex, uv + t * vec2(2.0, 2.0)).rgb;
    vec3 d = texture(sMainTex, uv + t * vec2(-2.0, 0.0)).rgb;
    vec3 e = texture(sMainTex, uv).rgb;
    vec3 f = texture(sMainTex, uv + t * vec2(2.0, 0.0)).rgb;
    vec3 g = texture(sMainTex, uv + t * vec2(-2.0, -2.0)).rgb;
    vec3 h = texture(sMainTex, uv + t * vec2(0.0, -2.0)).rgb;
    vec3 i = texture(sMainTex, uv + t * vec2(2.0, -2.0)).rgb;
    vec3 j = texture(sMainTex, uv + t * vec2(-1.0, 1.0)).rgb;
    vec3 k = texture(sMainTex, uv + t * vec2(1.0, 1.0)).rgb;
    vec3 l = texture(sMainTex, uv + t * vec2(-1.0, -1.0)).rgb;
    vec3 m = texture(sMainTex, uv + t * vec2(1.0, -1.0)).rgb;
    vec3 color = (j + k + l + m) * 0.125;
    color += (a + b + d + e) * 0.03125;
    color += (b + c + e + f) * 0.03125;
    color += (d + e + g + h) * 0.03125;
    color += (e + f + h + i) * 0.03125;
    fragColor = vec4(color, 1.0);
}
