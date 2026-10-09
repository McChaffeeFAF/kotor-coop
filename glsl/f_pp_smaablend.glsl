#include "u_screeneffect.glsl"

// SMAA 1x-style morphological AA, pass 3: neighbourhood blending. Each pixel
// takes the strongest of its four weights (see f_pp_smaaweights) and blends
// with that neighbour through one bilinear fetch.

uniform sampler2D sMainTex;  // colour
uniform sampler2D sHilights; // blending weights

noperspective in vec2 fragUV1;

out vec4 fragColor;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(sHilights, 0);
    vec4 here = texelFetch(sHilights, p, 0);
    float up = here.r;
    float left = here.b;
    float down = p.y > 0 ? texelFetch(sHilights, p + ivec2(0, -1), 0).g : 0.0;
    float right = p.x + 1 < size.x ? texelFetch(sHilights, p + ivec2(1, 0), 0).a : 0.0;

    float vertical = max(up, down);
    float horizontal = max(left, right);
    if (max(vertical, horizontal) < 1e-5) {
        fragColor = texelFetch(sMainTex, p, 0);
        return;
    }
    vec2 offset = vertical > horizontal
                      ? vec2(0.0, up > down ? up : -down)
                      : vec2(left > right ? -left : right, 0.0);
    fragColor = texture(sMainTex, fragUV1 + offset * uScreenResolutionRcp);
}
