#include "u_globals.glsl"
#include "u_locals.glsl"

#include "i_oit.glsl"

// A projected beam (upstream #356): the silhouette shadow volume of a mesh,
// in a flat translucent colour.

layout(location = 0) out vec4 fragColor1;
layout(location = 1) out vec4 fragColor2;

void main() {
    float alpha = uColor.a;
    if (alpha == 0.0) {
        discard;
    }
    float w = OIT_weight(gl_FragCoord.z, alpha);
    fragColor1 = vec4(uColor.rgb * w, alpha);
    fragColor2 = vec4(w, 0.0, 0.0, 0.0);
}
