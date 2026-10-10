#include "u_bones.glsl"
#include "u_dangly.glsl"
#include "u_globals.glsl"
#include "u_locals.glsl"
#include "u_saber.glsl"

#include "i_hash.glsl"
#include "i_lighting.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV1;
layout(location = 3) in vec2 aUV2;
layout(location = 4) in vec3 aBitangent;
layout(location = 5) in vec3 aTangent;
layout(location = 6) in vec3 aTanSpaceNormal;
layout(location = 7) in vec4 aBoneIndices;
layout(location = 8) in vec4 aBoneWeights;

out vec4 fragPos;
out vec4 fragPosWorld;
out vec3 fragNormalWorld;
out vec2 fragUV1;
out vec2 fragUV2;
out mat3 fragTBN;
out vec3 fragObjectAmbient;
out vec3 fragObjectDiffuse;
out vec3 fragObjectLightDir;

void main() {
    vec4 P = vec4(aPosition, 1.0);
    vec4 N = vec4(aNormal, 0.0);
    vec3 tangent = aTangent;
    vec3 bitangent = aBitangent;
    vec3 tanSpaceNormal = aTanSpaceNormal;

    if (isFeatureEnabled(FEATURE_SKIN)) {
        int i1 = max(0, int(aBoneIndices[0]));
        int i2 = max(0, int(aBoneIndices[1]));
        int i3 = max(0, int(aBoneIndices[2]));
        int i4 = max(0, int(aBoneIndices[3]));

        float w1 = aBoneWeights[0];
        float w2 = aBoneWeights[1];
        float w3 = aBoneWeights[2];
        float w4 = aBoneWeights[3];

        P =
            (uBones[i1] * P) * w1 +
            (uBones[i2] * P) * w2 +
            (uBones[i3] * P) * w3 +
            (uBones[i4] * P) * w4;

        N =
            (uBones[i1] * N) * w1 +
            (uBones[i2] * N) * w2 +
            (uBones[i3] * N) * w3 +
            (uBones[i4] * N) * w4;

        if (isFeatureEnabled(FEATURE_SURFACEDETAIL)) {
            // Surface detail: the tangent frame follows the bones too.
            mat3 skin = mat3(uBones[i1]) * w1 + mat3(uBones[i2]) * w2 + mat3(uBones[i3]) * w3 + mat3(uBones[i4]) * w4;
            tangent = skin * tangent;
            bitangent = skin * bitangent;
            tanSpaceNormal = skin * tanSpaceNormal;
        }

        fragPos = P;

    } else if (isFeatureEnabled(FEATURE_DANGLY)) {
        fragPos = uDanglyPositions[gl_VertexID];

    } else if (isFeatureEnabled(FEATURE_SABER)) {
        float signum = 2.0 * (gl_VertexID / 88) - 1.0;
        float hdist = ((gl_VertexID % 88) / 4) / 21.0;
        float vdist = (gl_VertexID % 4) / 3.0;
        fragPos = vec4(P.xyz + 0.5 * signum * hdist * vdist * uSaberDisplacement.xyz, 1.0);

    } else {
        fragPos = P;
    }

    fragPosWorld = uModel * fragPos;

    mat3 normalMatrix = transpose(mat3(uModelInv));
    fragNormalWorld = normalize(normalMatrix * N.xyz);
    if (isFeatureEnabled(FEATURE_SHELL)) {
        // A programmed body overlay: the surface pushed out along its normal.
        fragPosWorld.xyz += fragNormalWorld * uSurfaceParams.y;
    }

    fragUV1 = aUV1;
    if (uUVJitterAmplitude > 0.0 && uUVJitterSpeed > 0.0) {
        // Each vertex wanders within the jitter box around its animated UV,
        // re-picking a target 30 times a second at full speed.
        float steps = uUVJitterTime * 30.0 * min(1.0, uUVJitterSpeed);
        float s0 = floor(steps);
        float id = float(gl_VertexID);
        vec2 from = vec2(hash(vec2(id, s0)), hash(vec2(id + 0.5, s0)));
        vec2 to = vec2(hash(vec2(id, s0 + 1.0)), hash(vec2(id + 0.5, s0 + 1.0)));
        fragUV1 += uUVJitterAmplitude * (2.0 * mix(from, to, steps - s0) - 1.0);
    }
    fragUV2 = aUV2;

    if (isFeatureEnabled(FEATURE_NORMALMAP) || isFeatureEnabled(FEATURE_BUMPMAP)) {
        vec3 T = normalize(normalMatrix * tangent);
        vec3 B = normalize(normalMatrix * bitangent);
        vec3 TSN = normalize(normalMatrix * tanSpaceNormal);
        fragTBN = mat3(T, B, TSN);
    }

    // Dynamic objects are lit per vertex, as the fixed-function lights of the
    // original: each vertex sums its lights and clamps the sum to 1, and the
    // result is interpolated across the triangle. A very strong light (the
    // character-generation key light has multiplier 50) then shades a face
    // smoothly instead of cutting a hard terminator along every crease.
    fragObjectAmbient = vec3(0.0);
    fragObjectDiffuse = vec3(0.0);
    fragObjectLightDir = vec3(0.0);
    if (isFeatureEnabled(FEATURE_OBJECTLIGHTS)) {
        vec3 ambient = vec3(0.0);
        vec3 diffuse = vec3(0.0);
        if (!isFeatureEnabled(FEATURE_LIGHTMAP) && !isFeatureEnabled(FEATURE_STATIC)) {
            ambient += uAmbientColor.rgb * uWorldAmbientColor.rgb;
        }
        for (int i = 0; i < uNumObjectLights; ++i) {
            vec3 lightPos = uObjectLights[i].position.xyz - fragPosWorld.xyz;
            float lightDist = length(lightPos);
            vec3 lightDir = lightPos / max(1e-4, lightDist);
            float attenuation = objectLightAttenuation(uObjectLights[i].radius, lightDist);
            vec3 lightColor = uObjectLights[i].multiplier * uObjectLights[i].color.rgb;
            if (uObjectLights[i].ambientOnly) {
                ambient += attenuation * uAmbientColor.rgb * lightColor;
            } else {
                vec3 lit = max(0.0, dot(fragNormalWorld, lightDir)) * attenuation * uDiffuseColor.rgb * lightColor;
                diffuse += lit;
                // Surface detail: where most of the vertex light comes from.
                fragObjectLightDir += (lit.r + lit.g + lit.b) * lightDir;
            }
        }
        fragObjectAmbient = clamp(ambient, 0.0, 1.0);
        fragObjectDiffuse = clamp(diffuse, vec3(0.0), vec3(1.0) - fragObjectAmbient);
    }

    gl_Position = uProjection * uView * fragPosWorld;
}
