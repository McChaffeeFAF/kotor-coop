#include "u_screeneffect.glsl"

// Auto exposure, step 2 (one texel): the metered average luma gives the
// exposure (EV) that brings it to the target, clamped to a small range; it
// eases from last frame's value (sHilights) at the adaptation speed.
// uExposureParams: dt, speed brightening, speed darkening, history valid.
// uExposureRange: min EV, max EV, meter width, meter height. uToneParams.w
// is the target luma.

uniform sampler2D sMainTex;
uniform sampler2D sHilights;

out vec4 fragColor;

void main() {
    ivec2 size = ivec2(uExposureRange.zw);
    float sumLog = 0.0;
    float sumWeight = 0.0;
    for (int y = 0; y < size.y; ++y) {
        for (int x = 0; x < size.x; ++x) {
            vec2 m = texelFetch(sMainTex, ivec2(x, y), 0).rg;
            sumLog += m.r;
            sumWeight += m.g;
        }
    }
    float average = exp2(sumLog / max(sumWeight, 1e-4));
    float target = clamp(log2(uToneParams.w / max(average, 1e-4)), uExposureRange.x, uExposureRange.y);
    float previous = texelFetch(sHilights, ivec2(0, 0), 0).r;
    float ev = target;
    if (uExposureParams.w > 0.5) {
        float speed = target > previous ? uExposureParams.y : uExposureParams.z;
        ev = previous + (target - previous) * (1.0 - exp(-uExposureParams.x * speed));
    }
    fragColor = vec4(ev, average, target, 1.0);
}
