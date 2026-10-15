#include "u_globals.glsl"
#include "u_locals.glsl"

#include "i_fog.glsl"
#include "i_forcesight.glsl"
#include "i_hash.glsl"
#include "i_lighting.glsl"
#include "i_math.glsl"

#include "i_envmap.glsl"
#include "i_hashedalpha.glsl"
#include "i_normalmap.glsl"
#include "i_shadowmap.glsl"

uniform sampler2D sMainTex;
uniform sampler2D sNormalMap;
uniform sampler2D sMaterialMap;
uniform sampler2D sLightmap;
uniform sampler2D sEnvMap;
uniform sampler2DArray sBumpMapArray;
uniform sampler2DArray sShadowMap;
uniform samplerCube sEnvMapCube;
uniform samplerCube sShadowMapCube;

in vec4 fragPosWorld;
in vec3 fragNormalWorld;
in mat3 fragTBN;
in vec2 fragUV1;
in vec2 fragUV2;
in vec3 fragObjectAmbient;
in vec3 fragObjectDiffuse;
in vec3 fragObjectLightDir;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragHilights;

void main() {
    vec2 uv = vec2(uUV * vec3(fragUV1, 1.0));
    vec4 mainTexSample = texture(sMainTex, uv);
    if (!isFeatureEnabled(FEATURE_ENVMAP)) {
        if (isFeatureEnabled(FEATURE_HASHEDALPHATEST)) {
            hashedAlphaTest(mainTexSample.a, fragPosWorld.xyz);
        } else if (mainTexSample.a == 0.0) {
            discard;
        }
    }
    vec3 geomNormal = normalize(fragNormalWorld);
    vec3 normal = geomNormal;
    if (isFeatureEnabled(FEATURE_NORMALMAP)) {
        normal = normalFromNormalMap(sNormalMap, uv, fragTBN);
    } else if (isFeatureEnabled(FEATURE_BUMPMAP)) {
        normal = normalFromBumpMap(sBumpMapArray, uv, fragTBN);
    }
    vec3 viewPos = uCameraPosition.xyz - fragPosWorld.xyz;
    vec3 viewDir = normalize(viewPos);

    // Surface detail (Tier 4): generated normal and material maps. The
    // baked light (lightmap) and the per-vertex object light carry no
    // direction per pixel, so the mapped normal shades them against the
    // direction their light mostly comes from: the object lights' weighted
    // direction, or for baked light the surface normal tilted towards the
    // sky. Off, none of this runs.
    bool surfaceDetail = isFeatureEnabled(FEATURE_SURFACEDETAIL);
    float surfaceStrength = uSurfaceParams.x;
    float detail = 1.0;
    float roughness = 1.0;
    float specularHint = 0.0;
    vec3 keyDir = geomNormal;
    if (surfaceDetail) {
        if (isFeatureEnabled(FEATURE_NORMALMAP)) {
            normal = normalize(mix(geomNormal, normal, surfaceStrength));
        }
        if (isFeatureEnabled(FEATURE_MATERIALMAP)) {
            vec4 materialSample = texture(sMaterialMap, uv);
            roughness = materialSample.r;
            specularHint = materialSample.g;
        }
        // Baked light: the surface normal, tilted a little towards the sky
        // (and off-axis, so floors show relief too); always mostly along
        // the normal, so ceilings and walls get a stable direction.
        keyDir = (isFeatureEnabled(FEATURE_OBJECTLIGHTS) && dot(fragObjectLightDir, fragObjectLightDir) > 1e-8)
                     ? normalize(fragObjectLightDir)
                     : normalize(geomNormal + 0.5 * vec3(0.35, 0.25, 0.9));
        // Exactly 1 where the mapped normal is the vertex normal.
        detail = clamp((max(dot(normal, keyDir), 0.0) + 0.25) / (max(dot(geomNormal, keyDir), 0.0) + 0.25), 0.35, 1.6);
    }

    vec3 ambient = vec3(0.0);
    vec3 diffuse = uSelfIllumColor.rgb;
    vec3 keyLight = vec3(0.0);
    if (isFeatureEnabled(FEATURE_LIGHTMAP)) {
        vec4 lightmapSample = texture(sLightmap, fragUV2);
        diffuse += detail * lightmapSample.rgb;
        keyLight += lightmapSample.rgb;
    } else if (!isFeatureEnabled(FEATURE_STATIC) && !isFeatureEnabled(FEATURE_OBJECTLIGHTS)) {
        ambient += uAmbientColor.rgb * uWorldAmbientColor.rgb;
    }
    if (isFeatureEnabled(FEATURE_OBJECTLIGHTS)) {
        // Lit per vertex in v_model (fixed-function look).
        ambient += (surfaceDetail ? mix(1.0, detail, 0.5) : 1.0) * fragObjectAmbient;
        diffuse += detail * fragObjectDiffuse;
        keyLight += fragObjectDiffuse;
    }
    for (int i = 0; i < uNumLights; ++i) {
        if (isFeatureEnabled(FEATURE_OBJECTLIGHTS)) {
            break;
        }
        if (isFeatureEnabled(FEATURE_STATIC) && uLights[i].dynamicType != LIGHT_DYNAMIC_TYPE_ALL) {
            continue;
        }
        vec3 lightPos = uLights[i].position.xyz - fragPosWorld.xyz;
        float lightDist = length(lightPos);
        if (lightDist > uLights[i].radius) { // reach ends at the radius (was compared with radius squared)
            continue;
        }
        vec3 lightDir = lightPos / max(1e-4, lightDist);
        float diff = max(0.0, dot(normal, lightDir));
        float attenuation = lightAttenuationQuadratic(uLights[i], lightDist);
        vec3 lightColor = uLights[i].color.rgb;
        if (uLights[i].ambientOnly) {
            ambient += uLights[i].multiplier * attenuation * uAmbientColor.rgb * lightColor;
        } else {
            diffuse += uLights[i].multiplier * diff * attenuation * uDiffuseColor.rgb * lightColor;
        }
    }
    float shadow = isFeatureEnabled(FEATURE_SHADOWS)
                       ? getShadow(viewPos, fragPosWorld.xyz, surfaceDetail ? geomNormal : normal, sShadowMap, sShadowMapCube)
                       : 0.0;
    vec3 color = min(vec3(uLightClamp), (ambient + (1.0 - shadow) * max(vec3(0.0), diffuse))) * mainTexSample.rgb;
    float envScale = 1.0;
    if (surfaceDetail && isFeatureEnabled(FEATURE_MATERIALMAP)) {
        // A soft highlight from the key light, as sharp as the material is smooth.
        vec3 H = normalize(keyDir + viewDir);
        float shininess = exp2(mix(7.0, 2.0, roughness));
        float highlight = pow(max(dot(normal, H), 0.0), shininess) * mix(1.0, 0.2, roughness);
        color += (1.0 - shadow) * surfaceStrength * 0.6 * specularHint * highlight * min(keyLight, vec3(1.0));
        // Shiny parts reflect more, rough parts less.
        envScale = mix(1.0, clamp((1.0 - 0.6 * roughness) * (0.5 + specularHint), 0.0, 1.5), surfaceStrength);
    }
    if (uForceSight.x > 0.5) {
        // Force Sight (upstream #356): the lit colour turns grey.
        color = forceSightGrey(color);
    } else if (isFeatureEnabled(FEATURE_ENVMAP)) {
        vec3 R = reflect(-viewDir, normal);
        vec4 envmapSample = sampleEnvMap(sEnvMap, sEnvMapCube, R);
        color += envmapSample.rgb * (1.0 - mainTexSample.a) * envScale;
    }
    if (isFeatureEnabled(FEATURE_FOG)) {
        float fog = getFog(fragPosWorld.xyz);
        color = mix(color, uFogColor.rgb, fog);
    }
    vec3 hilights = step(1e-4, uSelfIllumColor.rgb) * step(0.95, color) * color;
    // With reflections on, the alpha carries the env-map strength for the
    // SSR pass (which sets it back to 1); otherwise it is 1 as before.
    float reflectivity = isFeatureEnabled(FEATURE_ENVMAP) ? min(1.0, (1.0 - mainTexSample.a) * envScale) : 0.0;
    fragColor = vec4(color, uParticleParams.y > 0.5 ? reflectivity : 1.0);
    fragHilights = vec4(hilights, 1.0);
}
