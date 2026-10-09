const int NUM_SHADOW_CASCADES = 4;
const int NUM_PCF_SAMPLES = 20;

const float PCF_SAMPLE_RADIUS = 0.1;

const vec3 PCF_SAMPLE_OFFSETS[20] = vec3[](
    vec3(1, 1, 1), vec3(1, -1, 1), vec3(-1, -1, 1), vec3(-1, 1, 1),
    vec3(1, 1, -1), vec3(1, -1, -1), vec3(-1, -1, -1), vec3(-1, 1, -1),
    vec3(1, 1, 0), vec3(1, -1, 0), vec3(-1, -1, 0), vec3(-1, 1, 0),
    vec3(1, 0, 1), vec3(-1, 0, 1), vec3(1, 0, -1), vec3(-1, 0, -1),
    vec3(0, 1, 1), vec3(0, -1, 1), vec3(0, -1, -1), vec3(0, 1, -1));

float getDirectionalLightShadow(vec3 viewPos, vec3 worldPos, sampler2DArray tex) {
    int cascade = NUM_SHADOW_CASCADES - 1;
    for (int i = 0; i < NUM_SHADOW_CASCADES; ++i) {
        if (abs(viewPos.z) < uShadowCascadeFarPlanes[i]) {
            cascade = i;
            break;
        }
    }

    vec4 lightSpacePos = uShadowLightSpace[cascade] * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = 0.5 * projCoords + 0.5;

    float currentDepth = projCoords.z;
    if (currentDepth > 1.0)
        return 0.0;

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(tex, 0));
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(tex, vec3(projCoords.xy + vec2(x, y) * texelSize, cascade)).r;
            shadow += currentDepth > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;

    return shadow;
}

float getPointLightShadow(vec3 worldPos, samplerCube tex) {
    vec3 fragToLight = worldPos - uShadowLightPosition.xyz;
    float currentDepth = length(fragToLight);

    float shadow = 0.0;
    for (int i = 0; i < NUM_PCF_SAMPLES; ++i) {
        float closestDepth = 2500.0 * texture(tex, fragToLight + PCF_SAMPLE_RADIUS * PCF_SAMPLE_OFFSETS[i]).r;
        shadow += currentDepth > closestDepth ? 1.0 : 0.0;
    }
    shadow /= NUM_PCF_SAMPLES;
    shadow *= 1.0 - smoothstep(uShadowRadius, 2.0 * uShadowRadius, currentDepth);

    return shadow;
}

// Soft shadows (Enhanced preset): percentage-closer soft shadows
// (Fernando, "Percentage-Closer Soft Shadows", 2005). A blocker search
// estimates the mean occluder depth, the penumbra grows with the receiver's
// distance behind it, and a rotated Vogel-disk PCF of that width filters the
// map. Widths are world-space, so the penumbra holds across cascades, and the
// last part of each cascade blends into the next one.
//
// The filter never gets narrower than the retail one: a contact shadow is
// the retail shadow, and a shadow cast from further away only gets wider
// and lighter at its edge, never darker or longer. The blocker search
// covers the widest filter, so a lit texel next to a shadow is never
// skipped by the early-out while the filter would still reach a blocker.

const int SOFT_BLOCKER_SAMPLES = 16;
const int SOFT_PCF_SAMPLES = 24;
const float SOFT_SUN_TAN = 0.035;          // tangent of the sun's apparent radius
const float SOFT_MAX_PENUMBRA = 0.5;       // metres, filter radius
const float SOFT_POINT_LIGHT_RADIUS = 0.2; // metres
// The retail point-light filter samples a ring 0.10-0.14 m around the
// lookup and has no centre tap, so a thin caster (a leg) leaves only a faint
// shadow. A disk needs about 0.24 m to spread a thin caster as thinly.
const float SOFT_POINT_MIN_RADIUS = 0.24;  // metres
// The retail directional filter is a 3x3 texel box.
const float SOFT_DIR_MIN_TEXELS = 1.5;
const float SOFT_BIAS = 0.01;              // metres
const float SOFT_CASCADE_BLEND = 0.1;      // last tenth of a cascade
const float GOLDEN_ANGLE = 2.39996323;

float softShadowNoise() {
    // Interleaved gradient noise (Jimenez 2014), shifted per frame under TAA.
    vec2 p = gl_FragCoord.xy + 5.588238 * uShadowNoiseFrame;
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

vec2 vogelDisk(int i, int count, float rotation) {
    float r = sqrt((float(i) + 0.5) / float(count));
    float theta = float(i) * GOLDEN_ANGLE + rotation;
    return r * vec2(cos(theta), sin(theta));
}

float getDirectionalLightSoftShadowInCascade(vec3 worldPos, sampler2DArray tex, int cascade, float rotation) {
    mat4 m = uShadowLightSpace[cascade];
    vec4 lightSpacePos = m * vec4(worldPos, 1.0);
    vec3 projCoords = 0.5 * (lightSpacePos.xyz / lightSpacePos.w) + 0.5;
    if (projCoords.z > 1.0) {
        return 0.0;
    }
    // Orthographic: map units per metre from the matrix rows.
    float uvPerMetre = 0.5 * length(vec3(m[0][0], m[1][0], m[2][0]));
    float depthPerMetre = max(0.5 * length(vec3(m[0][2], m[1][2], m[2][2])), 1e-6);
    float receiver = projCoords.z - SOFT_BIAS * depthPerMetre;
    float texel = 1.0 / float(textureSize(tex, 0).x);
    float minUV = SOFT_DIR_MIN_TEXELS * texel;
    float maxUV = max(SOFT_MAX_PENUMBRA * uShadowSoftness * uvPerMetre, minUV);

    float blockerSum = 0.0;
    int blockers = 0;
    for (int i = 0; i < SOFT_BLOCKER_SAMPLES; ++i) {
        vec2 offset = vogelDisk(i, SOFT_BLOCKER_SAMPLES, rotation) * maxUV;
        float z = texture(tex, vec3(projCoords.xy + offset, cascade)).r;
        if (z < receiver) {
            blockerSum += z;
            ++blockers;
        }
    }
    if (blockers == 0) {
        return 0.0;
    }
    float blocker = blockerSum / float(blockers);
    float distanceMetres = (receiver - blocker) / depthPerMetre;
    float filterUV = clamp(distanceMetres * SOFT_SUN_TAN * uShadowSoftness * uvPerMetre, minUV, maxUV);

    float shadow = 0.0;
    for (int i = 0; i < SOFT_PCF_SAMPLES; ++i) {
        vec2 offset = vogelDisk(i, SOFT_PCF_SAMPLES, rotation) * filterUV;
        float z = texture(tex, vec3(projCoords.xy + offset, cascade)).r;
        shadow += receiver > z ? 1.0 : 0.0;
    }
    return shadow / float(SOFT_PCF_SAMPLES);
}

float getDirectionalLightSoftShadow(vec3 viewPos, vec3 worldPos, sampler2DArray tex) {
    float distance = abs(viewPos.z);
    int cascade = NUM_SHADOW_CASCADES - 1;
    for (int i = 0; i < NUM_SHADOW_CASCADES; ++i) {
        if (distance < uShadowCascadeFarPlanes[i]) {
            cascade = i;
            break;
        }
    }
    float rotation = 2.0 * PI * softShadowNoise();
    float shadow = getDirectionalLightSoftShadowInCascade(worldPos, tex, cascade, rotation);
    if (cascade < NUM_SHADOW_CASCADES - 1) {
        float near = cascade > 0 ? uShadowCascadeFarPlanes[cascade - 1] : 0.0;
        float far = uShadowCascadeFarPlanes[cascade];
        float band = SOFT_CASCADE_BLEND * (far - near);
        float t = clamp((distance - (far - band)) / max(band, 1e-4), 0.0, 1.0);
        if (t > 0.0) {
            float next = getDirectionalLightSoftShadowInCascade(worldPos, tex, cascade + 1, rotation);
            shadow = mix(shadow, next, t);
        }
    }
    return shadow;
}

float getPointLightSoftShadow(vec3 worldPos, samplerCube tex) {
    vec3 fragToLight = worldPos - uShadowLightPosition.xyz;
    float distance = length(fragToLight);
    float receiver = distance - SOFT_BIAS;
    vec3 dir = fragToLight / max(distance, 1e-4);
    // A tangent frame around the light-to-fragment direction; offsets are
    // metres at the receiver, as in the retail filter.
    vec3 helper = abs(dir.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, dir));
    vec3 bitangent = cross(dir, tangent);
    float rotation = 2.0 * PI * softShadowNoise();
    float minRadius = min(SOFT_POINT_MIN_RADIUS, 0.5 * distance);
    float maxRadius = clamp(SOFT_MAX_PENUMBRA * uShadowSoftness, minRadius, max(minRadius, 0.5 * distance));

    float blockerSum = 0.0;
    int blockers = 0;
    for (int i = 0; i < SOFT_BLOCKER_SAMPLES; ++i) {
        vec2 o = vogelDisk(i, SOFT_BLOCKER_SAMPLES, rotation) * maxRadius;
        float z = 2500.0 * texture(tex, fragToLight + o.x * tangent + o.y * bitangent).r;
        if (z < receiver) {
            blockerSum += z;
            ++blockers;
        }
    }
    if (blockers == 0) {
        return 0.0;
    }
    float blocker = max(blockerSum / float(blockers), 0.05);
    float lightRadius = SOFT_POINT_LIGHT_RADIUS * uShadowSoftness;
    float penumbra = clamp(lightRadius * (receiver - blocker) / blocker, minRadius, maxRadius);

    float shadow = 0.0;
    for (int i = 0; i < SOFT_PCF_SAMPLES; ++i) {
        vec2 o = vogelDisk(i, SOFT_PCF_SAMPLES, rotation) * penumbra;
        float z = 2500.0 * texture(tex, fragToLight + o.x * tangent + o.y * bitangent).r;
        shadow += receiver > z ? 1.0 : 0.0;
    }
    shadow /= float(SOFT_PCF_SAMPLES);
    shadow *= 1.0 - smoothstep(uShadowRadius, 2.0 * uShadowRadius, distance);
    return shadow;
}

float getShadow(vec3 viewPos, vec3 worldPos, vec3 normal,
                sampler2DArray tex, samplerCube cubeTex) {
    if (uShadowSoftness > 0.0) {
        float soft = (uShadowLightPosition.w == 0.0)
                         ? getDirectionalLightSoftShadow(viewPos, worldPos, tex)
                         : getPointLightSoftShadow(worldPos, cubeTex);
        return soft * uShadowStrength;
    }
    float shadow = (uShadowLightPosition.w == 0.0)
                       ? getDirectionalLightShadow(viewPos, worldPos, tex)
                       : getPointLightShadow(worldPos, cubeTex);
    shadow *= uShadowStrength;
    return shadow;
}
