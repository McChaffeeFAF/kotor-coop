uniform sampler2D sMainTex;
uniform sampler2D sHilights;
uniform sampler2D sOITAccum;
uniform sampler2D sOITRevealage;

noperspective in vec2 fragUV1;

out vec4 fragColor;

void main() {
    vec4 mainTexSample = texture(sMainTex, fragUV1);
    vec4 hilightsSample = texture(sHilights, fragUV1);
    vec4 oitAccumSample = texture(sOITAccum, fragUV1);
    vec4 oitRevealageSample = texture(sOITRevealage, fragUV1);

    vec3 accumColor = oitAccumSample.rgb;
    float accumWeight = oitRevealageSample.r;
    float revealage = oitAccumSample.a;

    float alpha = 1.0 - revealage;
    vec3 color = alpha * (accumColor.rgb / max(0.0001, accumWeight)) + (1.0 - alpha) * (mainTexSample.rgb + hilightsSample.rgb);

    // Additive particle light (see f_oit_particles), over everything.
    vec3 glow4 = vec3(oitRevealageSample.g, oitRevealageSample.b, -log(max(oitRevealageSample.a, 1e-6)));
    color += pow(max(glow4, vec3(0.0)), vec3(0.25));

    fragColor = vec4(color, alpha + mainTexSample.a);
}
