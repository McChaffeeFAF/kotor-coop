// Colour grading (design note graphics_preset): the frame through a
// 32x32x32 LUT stored as a 1024x32 strip of blue slices (sHilights),
// interpolated between the two nearest slices.

uniform sampler2D sMainTex;
uniform sampler2D sHilights;

noperspective in vec2 fragUV1;

out vec4 fragColor;

const float N = 32.0;

vec3 lutSlice(vec3 c, float slice) {
    vec2 uv = vec2((slice * N + c.r * (N - 1.0) + 0.5) / (N * N), (c.g * (N - 1.0) + 0.5) / N);
    return texture(sHilights, uv).rgb;
}

void main() {
    vec4 color = texture(sMainTex, fragUV1);
    vec3 c = clamp(color.rgb, 0.0, 1.0);
    float b = c.b * (N - 1.0);
    float lo = floor(b);
    float hi = min(lo + 1.0, N - 1.0);
    vec3 graded = mix(lutSlice(c, lo), lutSlice(c, hi), b - lo);
    fragColor = vec4(graded, color.a);
}
