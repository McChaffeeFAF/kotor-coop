#include "u_screeneffect.glsl"

// Tone mapping of the HDR frame (design note graphics_preset).
// uToneParams: x operator (0 none, 1 ACES, 2 AgX), y auto exposure on,
// z exposure bias (EV). The adapted EV is in sHilights (one texel).
// The frame's values are display-encoded (gamma 2.2, as the retail
// pipeline has them); the curves work on linear values.

uniform sampler2D sMainTex;
uniform sampler2D sHilights;

noperspective in vec2 fragUV1;

out vec4 fragColor;

const float GAMMA = 2.2;

// ACES filmic fit (Narkowicz 2015). Scaled so that linear mid grey (0.18)
// stays mid grey, and normalised to reach 1.0 at a linear input of 4.
vec3 acesFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 aces(vec3 linearColor) {
    const float scale = 0.72;
    vec3 white = acesFilm(vec3(4.0 * scale));
    return clamp(acesFilm(linearColor * scale) / white, 0.0, 1.0);
}

// AgX (Sobotka), with the common polynomial fit of the default contrast
// curve. Returns display-encoded values.
vec3 agxContrast(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 agx(vec3 linearColor) {
    const mat3 inset = mat3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEV = -12.47393;
    const float maxEV = 4.026069;
    vec3 v = inset * max(linearColor, vec3(1e-10));
    v = clamp(log2(v), minEV, maxEV);
    v = (v - minEV) / (maxEV - minEV);
    v = agxContrast(v);
    v = outset * v;
    return clamp(v, 0.0, 1.0);
}

void main() {
    vec4 color = texture(sMainTex, fragUV1);
    float ev = uToneParams.z;
    if (uToneParams.y > 0.5) {
        ev += texelFetch(sHilights, ivec2(0, 0), 0).r;
    }
    vec3 linearColor = pow(max(color.rgb, vec3(0.0)), vec3(GAMMA)) * exp2(ev);
    vec3 result;
    if (uToneParams.x > 1.5) {
        result = agx(linearColor); // display-encoded already
    } else if (uToneParams.x > 0.5) {
        result = pow(aces(linearColor), vec3(1.0 / GAMMA));
    } else {
        result = pow(clamp(linearColor, 0.0, 1.0), vec3(1.0 / GAMMA));
    }
    fragColor = vec4(result, color.a);
}
