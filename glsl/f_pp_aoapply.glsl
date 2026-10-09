#include "u_screeneffect.glsl"

// Darken the opaque colour by the AO term. The retro look has no separate
// ambient term, so the whole colour is scaled; uAOParams.w is the strength.

uniform sampler2D sMainTex;
uniform sampler2D sSSAO;

noperspective in vec2 fragUV1;

out vec4 fragColor;

void main() {
    vec4 color = texture(sMainTex, fragUV1);
    float ao = texelFetch(sSSAO, ivec2(gl_FragCoord.xy), 0).r;
    fragColor = vec4(color.rgb * mix(1.0, ao, uAOParams.w), color.a);
}
