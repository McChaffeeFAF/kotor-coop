#include "u_globals.glsl"
#include "u_locals.glsl"
#include "u_particles.glsl"

#include "i_luma.glsl"
#include "i_oit.glsl"

uniform sampler2D sMainTex;
uniform sampler2D sGBufDepth;

float linearizeDepth(float depth) {
    float z = depth * 2.0 - 1.0;
    return (2.0 * uClipNear * uClipFar) / (uClipFar + uClipNear - z * (uClipFar - uClipNear));
}

// Soft particles: 1 in the open, falling to 0 where the particle meets the
// opaque surface behind it (uParticleParams.x metres; 0 disables).
float softParticleFade() {
    if (uParticleParams.x <= 0.0) {
        return 1.0;
    }
    float scene = linearizeDepth(texelFetch(sGBufDepth, ivec2(gl_FragCoord.xy), 0).r);
    float particle = linearizeDepth(gl_FragCoord.z);
    return clamp((scene - particle) / uParticleParams.x, 0.0, 1.0);
}

in vec2 fragUV1;
flat in int fragInstanceID;

layout(location = 0) out vec4 fragColor1;
layout(location = 1) out vec4 fragColor2;

void main() {
    float oneOverGridX = 1.0 / uGridSize.x;
    float oneOverGridY = 1.0 / uGridSize.y;

    vec2 uv = fragUV1;
    uv.x *= oneOverGridX;
    uv.y *= oneOverGridY;

    int frame = int(uParticles[fragInstanceID].positionFrame.w);
    if (frame > 0) {
        uv.y += oneOverGridY * (frame / uGridSize.x);
        uv.x += oneOverGridX * (frame % uGridSize.x);
    }

    vec4 mainTexSample = texture(sMainTex, uv);
    vec3 mainTexColor = mainTexSample.rgb;
    float mainTexAlpha = mainTexSample.a;
    if (isFeatureEnabled(FEATURE_PREMULALPHA)) {
        // "Lighten" particles (flares, glows, sparks) add light and hide
        // nothing. They go to a glow sum in the revealage target and leave the
        // weighted average and the revealage alone. (As see-through layers,
        // overlapping flares resolved to an opaque white burst.) That target
        // adds r, g and b but multiplies a (cleared to 1), so blue goes in as
        // 1 - exp(-b): the product of those terms is exp(-sum of b).
        vec3 glow = uParticles[fragInstanceID].color.rgb * mainTexColor * mainTexSample.a * uParticles[fragInstanceID].color.a * softParticleFade();
        if (max(glow.r, max(glow.g, glow.b)) <= 0.0) {
            discard;
        }
        fragColor1 = vec4(0.0);
        // Lighten keeps the brightest light rather than the sum (the retail
        // blend is a max), so overlapping glows don't pile up to white. It is
        // approximated by a 4-norm: the sum of glow^4 here, its 4th root in
        // f_oit_blend.
        glow = glow * glow * glow * glow;
        fragColor2 = vec4(0.0, glow.r, glow.g, 1.0 - exp(-glow.b));
        return;
    }
    vec3 objectColor = uParticles[fragInstanceID].color.rgb * mainTexColor;
    float objectAlpha = uParticles[fragInstanceID].color.a * mainTexAlpha * softParticleFade();
    if (objectAlpha == 0.0) {
        discard;
    }

    float w = OIT_weight(gl_FragCoord.z, objectAlpha);
    fragColor1 = vec4(objectColor * w, objectAlpha);
    fragColor2 = vec4(w, 0.0, 0.0, 0.0);
}
