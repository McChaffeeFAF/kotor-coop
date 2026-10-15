#include "u_screeneffect.glsl"

// The speed blur (upstream #356): the current frame (sMainTex) blended with
// the up-sampled 128x128 history of the earlier output (sLightmap), history
// weighted by the ratio (uFrameEffectParams.x), the frame by the rest.

uniform sampler2D sMainTex;
uniform sampler2D sLightmap;

noperspective in vec2 fragUV1;

out vec4 fragColor;

void main() {
    vec4 current = texture(sMainTex, fragUV1);
    vec3 history = texture(sLightmap, fragUV1).rgb;
    fragColor = vec4(mix(current.rgb, history, uFrameEffectParams.x), current.a);
}
