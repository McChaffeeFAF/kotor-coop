#include "u_globals.glsl"

#include "i_math.glsl"
#include "i_lighting.glsl"
#include "i_shadowmap.glsl"

// Volumetric light (design note graphics_preset), at half resolution: a
// ray march from the camera to the opaque surface (at most uVolumeParams.w
// metres) through a uniform medium lit by the scene lights and the sun.
// The shadow light's shadow map shadows the medium. Out: rgb in-scattered
// light, a transmittance.
//
// uVolumeParams: density (1/m), intensity, phase anisotropy g, max distance.
// uVolumeTint: medium colour (rgb), steps (w).
// uVolumeShadow: shadow type (0 none, 1 sun, 2 point), noise frame, ambient
// weight, unused.

uniform sampler2D sGBufDepth;
uniform sampler2DArray sShadowMap;
uniform samplerCube sShadowMapCube;

out vec4 fragColor;

const int MAX_VOLUME_LIGHTS = 8;

float phaseHG(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5));
}

float interleavedGradientNoise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

float shadowAt(vec3 worldPos, vec3 viewPos) {
    if (uVolumeShadow.x > 1.5) {
        vec3 fragToLight = worldPos - uShadowLightPosition.xyz;
        float closest = 2500.0 * texture(sShadowMapCube, fragToLight).r;
        return length(fragToLight) - 0.05 > closest ? 1.0 : 0.0;
    }
    if (uVolumeShadow.x > 0.5) {
        int cascade = NUM_SHADOW_CASCADES - 1;
        for (int i = 0; i < NUM_SHADOW_CASCADES; ++i) {
            if (abs(viewPos.z) < uShadowCascadeFarPlanes[i]) {
                cascade = i;
                break;
            }
        }
        vec4 ls = uShadowLightSpace[cascade] * vec4(worldPos, 1.0);
        vec3 p = 0.5 * (ls.xyz / ls.w) + 0.5;
        if (p.z > 1.0) {
            return 0.0;
        }
        return p.z - 0.001 > texture(sShadowMap, vec3(p.xy, cascade)).r ? 1.0 : 0.0;
    }
    return 0.0;
}

void main() {
    ivec2 fullCoord = ivec2(gl_FragCoord.xy) * 2;
    vec2 fullSize = vec2(textureSize(sGBufDepth, 0));
    vec2 uv = (vec2(fullCoord) + 1.0) / fullSize;
    float depth = texelFetch(sGBufDepth, min(fullCoord, ivec2(fullSize) - 1), 0).r;

    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewEnd = uProjectionInv * clip;
    viewEnd /= viewEnd.w;
    float surfaceDistance = length(viewEnd.xyz);
    vec3 viewDir = viewEnd.xyz / max(surfaceDistance, 1e-4);
    float marchDistance = min(surfaceDistance, uVolumeParams.w);

    int steps = int(uVolumeTint.w);
    float stepLength = marchDistance / float(steps);
    float jitter = interleavedGradientNoise(gl_FragCoord.xy + 5.588238 * uVolumeShadow.y);
    vec3 cameraPos = uCameraPosition.xyz;
    vec3 worldDir = normalize((uViewInv * vec4(viewDir, 0.0)).xyz);

    // The lights that can reach the ray: the first MAX_VOLUME_LIGHTS of
    // the scene's (closest first), ambient-only lights excluded.
    int lightCount = min(uNumLights, MAX_VOLUME_LIGHTS);

    float density = uVolumeParams.x;
    float stepTransmittance = exp(-density * stepLength);
    vec3 inscatter = vec3(0.0);
    float transmittance = 1.0;
    for (int s = 0; s < steps; ++s) {
        float t = (float(s) + jitter) * stepLength;
        vec3 worldPos = cameraPos + worldDir * t;
        vec3 viewPos = viewDir * t;
        vec3 light = uWorldAmbientColor.rgb * uVolumeShadow.z;
        for (int i = 0; i < lightCount; ++i) {
            if (uLights[i].ambientOnly) {
                continue;
            }
            vec3 toLight;
            float attenuation;
            if (uLights[i].position.w == 0.0) {
                toLight = normalize(uLights[i].position.xyz);
                attenuation = 1.0;
            } else {
                vec3 d = uLights[i].position.xyz - worldPos;
                float dist = length(d);
                if (dist > uLights[i].radius) {
                    continue;
                }
                toLight = d / max(dist, 1e-4);
                attenuation = lightAttenuationQuadratic(uLights[i], dist) * (1.0 - smoothstep(0.6 * uLights[i].radius, uLights[i].radius, dist));
            }
            float visibility = 1.0;
            bool isShadowLight = uVolumeShadow.x > 1.5
                                     ? distance(uLights[i].position.xyz, uShadowLightPosition.xyz) < 0.05
                                     : (uVolumeShadow.x > 0.5 && uLights[i].position.w == 0.0);
            if (isShadowLight) {
                visibility = 1.0 - shadowAt(worldPos, viewPos) * uShadowStrength;
            }
            float phase = phaseHG(dot(worldDir, toLight), uVolumeParams.z) * 4.0 * PI; // 1 for isotropic
            light += uLights[i].color.rgb * uLights[i].multiplier * attenuation * visibility * phase;
        }
        // Energy-conserving step integration (Hillaire 2015).
        vec3 scattered = light * uVolumeTint.rgb;
        inscatter += transmittance * scattered * (1.0 - stepTransmittance);
        transmittance *= stepTransmittance;
    }
    fragColor = vec4(inscatter * uVolumeParams.y, transmittance);
}
