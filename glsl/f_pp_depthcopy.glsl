// ReShade-friendly mode: the opaque scene depth into the window's depth
// buffer (colour writes are off for this pass).

uniform sampler2D sGBufDepth;

out vec4 fragColor;

void main() {
    gl_FragDepth = texelFetch(sGBufDepth, ivec2(gl_FragCoord.xy), 0).r;
    fragColor = vec4(0.0);
}
