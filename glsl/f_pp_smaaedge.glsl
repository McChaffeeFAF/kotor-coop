#include "u_screeneffect.glsl"

#include "i_luma.glsl"

// SMAA 1x-style morphological AA, pass 1: luma edges with local contrast
// adaptation (after Jimenez et al., "SMAA: Enhanced Subpixel Morphological
// Antialiasing", 2012). Written for this pipeline; GL texture space, y up.
// r: an edge between this pixel and the one to its left.
// g: an edge between this pixel and the one above it.

const float THRESHOLD = 0.1;               // SMAA "high"
const float LOCAL_CONTRAST_FACTOR = 2.0;

uniform sampler2D sMainTex;

out vec4 fragColor;

float lumaAt(ivec2 p) {
    ivec2 size = textureSize(sMainTex, 0);
    return rgbToLuma(texelFetch(sMainTex, clamp(p, ivec2(0), size - 1), 0).rgb);
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    float l = lumaAt(p);
    float left = lumaAt(p + ivec2(-1, 0));
    float top = lumaAt(p + ivec2(0, 1));
    vec2 delta = abs(vec2(l) - vec2(left, top));
    vec2 edges = step(vec2(THRESHOLD), delta);
    if (edges.x + edges.y == 0.0) {
        discard;
    }
    float right = lumaAt(p + ivec2(1, 0));
    float bottom = lumaAt(p + ivec2(0, -1));
    vec2 maxDelta = max(delta, abs(vec2(l) - vec2(right, bottom)));
    float leftLeft = lumaAt(p + ivec2(-2, 0));
    float topTop = lumaAt(p + ivec2(0, 2));
    maxDelta = max(maxDelta, abs(vec2(left, top) - vec2(leftLeft, topTop)));
    float finalDelta = max(maxDelta.x, maxDelta.y);
    // An edge stays only where it is not dwarfed by a stronger neighbour.
    edges *= step(vec2(finalDelta), LOCAL_CONTRAST_FACTOR * delta);
    fragColor = vec4(edges, 0.0, 1.0);
}
