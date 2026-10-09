#include "u_screeneffect.glsl"

// Bloom, last step: the frame plus the summed bloom levels.
// uBloomParams.z is the intensity; uBloomLevels the number of levels summed.

uniform sampler2D sMainTex;
uniform sampler2D sHilights;

noperspective in vec2 fragUV1;

out vec4 fragColor;

void main() {
    vec4 color = texture(sMainTex, fragUV1);
    vec3 bloom = texture(sHilights, fragUV1).rgb / max(uBloomLevels, 1.0);
    fragColor = vec4(color.rgb + uBloomParams.z * bloom, color.a);
}
