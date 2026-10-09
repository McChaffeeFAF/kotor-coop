#include "u_screeneffect.glsl"

#include "i_coords.glsl"

// Screen-space reflections for the retro pipeline, with a static reflection
// probe behind them (design note graphics_preset). The opaque pass leaves
// each pixel's reflectivity in its colour's alpha (the env-map strength of
// env-mapped surfaces, 0 elsewhere). For reflective pixels a view-space ray
// is marched over the depth buffer (refined by bisection); a miss, or a
// fading hit, falls back to the area's probe: six 90-degree faces side by
// side in sEnvMap. The result replaces part of the surface colour, by
// reflectivity and a Schlick Fresnel term. Alpha leaves as 1.
// uSSRParams: strength, max distance (m), probe valid, noise frame.

const int STEPS = 32;
const int REFINE = 5;

uniform sampler2D sMainTex;
uniform sampler2D sGBufDepth;
uniform sampler2D sEnvMap;

noperspective in vec2 fragUV1;

out vec4 fragColor;

vec3 viewPosAt(vec2 uv) {
    return reconstructViewPos(uv, sGBufDepth);
}

vec3 viewNormal(vec2 uv, vec3 center) {
    vec2 t = uScreenResolutionRcp;
    vec3 l = viewPosAt(uv - vec2(t.x, 0.0));
    vec3 r = viewPosAt(uv + vec2(t.x, 0.0));
    vec3 b = viewPosAt(uv - vec2(0.0, t.y));
    vec3 u = viewPosAt(uv + vec2(0.0, t.y));
    vec3 dx = abs(r.z - center.z) < abs(center.z - l.z) ? r - center : center - l;
    vec3 dy = abs(u.z - center.z) < abs(center.z - b.z) ? u - center : center - b;
    return normalize(cross(dx, dy));
}

vec2 projectToUV(vec3 viewPos) {
    vec4 clip = uProjection * vec4(viewPos, 1.0);
    return clip.xy / clip.w * 0.5 + 0.5;
}

// The probe: face i looks along PROBE_DIRS[i] with PROBE_UPS[i] up, the
// same bases the scene graph renders them with.
const vec3 PROBE_DIRS[6] = vec3[](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1));
const vec3 PROBE_UPS[6] = vec3[](vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, -1, 0), vec3(0, 1, 0));

vec3 sampleProbe(vec3 dir) {
    vec3 a = abs(dir);
    int face;
    if (a.x >= a.y && a.x >= a.z) {
        face = dir.x > 0.0 ? 0 : 1;
    } else if (a.y >= a.z) {
        face = dir.y > 0.0 ? 2 : 3;
    } else {
        face = dir.z > 0.0 ? 4 : 5;
    }
    vec3 f = PROBE_DIRS[face];
    vec3 right = normalize(cross(f, PROBE_UPS[face]));
    vec3 up = cross(right, f);
    float d = dot(dir, f);
    vec2 st = vec2(dot(dir, right), dot(dir, up)) / max(d, 1e-4);
    vec2 size = vec2(textureSize(sEnvMap, 0));
    float halfTexel = 0.5 * 6.0 / size.x;
    float x = clamp(st.x * 0.5 + 0.5, halfTexel, 1.0 - halfTexel);
    float y = clamp(st.y * 0.5 + 0.5, 0.5 / size.y, 1.0 - 0.5 / size.y);
    return textureLod(sEnvMap, vec2((float(face) + x) / 6.0, y), 0.0).rgb;
}

float noise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec4 color = texture(sMainTex, fragUV1);
    float reflectivity = color.a;
    if (reflectivity <= 0.01) {
        fragColor = vec4(color.rgb, 1.0);
        return;
    }
    vec3 P = viewPosAt(fragUV1);
    vec3 N = viewNormal(fragUV1, P);
    vec3 V = normalize(P);
    vec3 R = normalize(reflect(V, N));

    float maxDistance = uSSRParams.y;
    float jitter = noise(gl_FragCoord.xy + 3.7 * uSSRParams.w);
    vec3 hitColor = vec3(0.0);
    float hitWeight = 0.0;
    float previous = 0.0;
    for (int i = 1; i <= STEPS; ++i) {
        // Steps grow with distance: fine near the surface, coarse far away.
        float s = (float(i) - 1.0 + jitter) / float(STEPS);
        float t = maxDistance * s * s + 0.05;
        vec3 Q = P + R * t;
        if (Q.z > -0.05) {
            break; // behind the camera
        }
        vec2 uv = projectToUV(Q);
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
            break;
        }
        float sceneZ = viewPosAt(uv).z;
        // Thick enough not to step through a wall between two samples.
        float thickness = max(0.15, 1.5 * (t - previous)) + 0.02 * t;
        if (Q.z < sceneZ && sceneZ - Q.z < thickness) {
            float lo = previous;
            float hi = t;
            for (int j = 0; j < REFINE; ++j) {
                float mid = 0.5 * (lo + hi);
                vec3 M = P + R * mid;
                vec2 muv = projectToUV(M);
                if (M.z < viewPosAt(muv).z) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            vec2 huv = projectToUV(P + R * hi);
            vec2 edge = smoothstep(vec2(0.0), vec2(0.08), huv) * (1.0 - smoothstep(vec2(0.92), vec2(1.0), huv));
            hitWeight = edge.x * edge.y * (1.0 - smoothstep(0.6 * maxDistance, maxDistance, hi));
            hitColor = textureLod(sMainTex, huv, 0.0).rgb;
            break;
        }
        previous = t;
    }
    vec3 reflection = hitColor;
    if (uSSRParams.z > 0.5) {
        vec3 worldR = normalize((uSSRViewInv * vec4(R, 0.0)).xyz);
        reflection = mix(sampleProbe(worldR), hitColor, hitWeight);
        hitWeight = 1.0;
    }
    float cosTheta = clamp(dot(-V, N), 0.0, 1.0);
    float fresnel = 0.45 + 0.55 * pow(1.0 - cosTheta, 5.0);
    // The env-map strength of KotOR's shiny floors sits around 0.3-0.6;
    // doubled, it lets the reflection carry over the retail env map.
    float amount = clamp(min(1.0, 2.0 * reflectivity) * fresnel * uSSRParams.x * hitWeight, 0.0, 1.0);
    fragColor = vec4(mix(color.rgb, reflection, amount), 1.0);
}
