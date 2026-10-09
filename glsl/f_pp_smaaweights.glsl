#include "u_screeneffect.glsl"

// SMAA 1x-style morphological AA, pass 2: blending weights.
//
// For each edge, search along it to both ends (one texel per step, up to
// MAX_SEARCH), read the crossing edges at the ends, and revectorize the
// silhouette as in MLAA/SMAA: an end with a crossing edge in one of the two
// rows anchors the line at that row's mid-height (+-0.5); the line runs to
// the far end (Z shape), meets zero at the middle (U and L shapes), or is
// flat (no crossing: not filtered). The area under the line inside this
// pixel is computed analytically, so no precomputed area or search texture
// is needed. There is no diagonal pass.
//
// Output, all in [0, 1]:
//   r: this pixel blends with the pixel above (edge on its top)
//   g: the pixel above blends with this one (same edge)
//   b: this pixel blends with the pixel to its left (edge on its left)
//   a: the pixel to the left blends with this one (same edge)

const int MAX_SEARCH = 16;

uniform sampler2D sMainTex; // edges

out vec4 fragColor;

vec2 edgesAt(ivec2 p) {
    ivec2 size = textureSize(sMainTex, 0);
    if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size))) {
        return vec2(0.0);
    }
    return texelFetch(sMainTex, p, 0).rg;
}

// Area of the segment a-b inside the pixel span [x0, x0 + 1]: (below, above).
vec2 segmentArea(vec2 a, vec2 b, float x0) {
    float lo = max(x0, a.x);
    float hi = min(x0 + 1.0, b.x);
    if (hi <= lo) {
        return vec2(0.0);
    }
    float slope = (b.y - a.y) / (b.x - a.x);
    float y0 = a.y + slope * (lo - a.x);
    float y1 = a.y + slope * (hi - a.x);
    vec2 result = vec2(0.0);
    if (y0 * y1 >= 0.0) {
        float area = 0.5 * (y0 + y1) * (hi - lo);
        result += area < 0.0 ? vec2(-area, 0.0) : vec2(0.0, area);
    } else {
        float xz = lo + (hi - lo) * y0 / (y0 - y1);
        float a0 = 0.5 * y0 * (xz - lo);
        float a1 = 0.5 * y1 * (hi - xz);
        result += a0 < 0.0 ? vec2(-a0, 0.0) : vec2(0.0, a0);
        result += a1 < 0.0 ? vec2(-a1, 0.0) : vec2(0.0, a1);
    }
    return result;
}

// h: -0.5 for a crossing edge on the negative side only, +0.5 on the
// positive side only, 0 for none or both.
float anchor(float negative, float positive) {
    bool n = negative > 0.5;
    bool p = positive > 0.5;
    return (n && !p) ? -0.5 : ((p && !n) ? 0.5 : 0.0);
}

vec2 patternArea(int d1, int d2, float h1, float h2) {
    float d = float(d1 + d2 + 1);
    float x0 = float(d1);
    float mid = 0.5 * d;
    if (h1 != 0.0 && h2 != 0.0) {
        if (h1 != h2) {
            return segmentArea(vec2(0.0, h1), vec2(d, h2), x0);
        }
        return segmentArea(vec2(0.0, h1), vec2(mid, 0.0), x0) + segmentArea(vec2(mid, 0.0), vec2(d, h2), x0);
    }
    if (h1 != 0.0) {
        return segmentArea(vec2(0.0, h1), vec2(mid, 0.0), x0);
    }
    if (h2 != 0.0) {
        return segmentArea(vec2(mid, 0.0), vec2(d, h2), x0);
    }
    return vec2(0.0);
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 e = edgesAt(p);
    vec4 weights = vec4(0.0);

    if (e.g > 0.5) {
        // Horizontal edge between this row (negative side) and the row above.
        int dl = 0;
        for (int i = 1; i <= MAX_SEARCH; ++i) {
            if (edgesAt(p + ivec2(-i, 0)).g < 0.5) break;
            dl = i;
        }
        int dr = 0;
        for (int i = 1; i <= MAX_SEARCH; ++i) {
            if (edgesAt(p + ivec2(i, 0)).g < 0.5) break;
            dr = i;
        }
        ivec2 l = p + ivec2(-dl, 0);
        ivec2 r = p + ivec2(dr + 1, 0);
        float h1 = dl < MAX_SEARCH ? anchor(edgesAt(l).r, edgesAt(l + ivec2(0, 1)).r) : 0.0;
        float h2 = dr < MAX_SEARCH ? anchor(edgesAt(r).r, edgesAt(r + ivec2(0, 1)).r) : 0.0;
        weights.rg = patternArea(dl, dr, h1, h2);
    }

    if (e.r > 0.5) {
        // Vertical edge between the column to the left (negative side) and this one.
        int db = 0;
        for (int i = 1; i <= MAX_SEARCH; ++i) {
            if (edgesAt(p + ivec2(0, -i)).r < 0.5) break;
            db = i;
        }
        int dt = 0;
        for (int i = 1; i <= MAX_SEARCH; ++i) {
            if (edgesAt(p + ivec2(0, i)).r < 0.5) break;
            dt = i;
        }
        // Crossing edges at the bottom end are the top edges of the row below
        // it; at the top end, the top edges of the top pixel's row.
        ivec2 b = p + ivec2(0, -db - 1);
        ivec2 t = p + ivec2(0, dt);
        float h1 = db < MAX_SEARCH ? anchor(edgesAt(b + ivec2(-1, 0)).g, edgesAt(b).g) : 0.0;
        float h2 = dt < MAX_SEARCH ? anchor(edgesAt(t + ivec2(-1, 0)).g, edgesAt(t).g) : 0.0;
        vec2 area = patternArea(db, dt, h1, h2);
        // Positive: the line is in this column, which takes the left colour.
        weights.b = area.y;
        weights.a = area.x;
    }

    fragColor = weights;
}
