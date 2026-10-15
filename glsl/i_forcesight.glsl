// Force Sight (upstream #356): every surface turns grey (the luminance of its
// lit colour, darkened), and glowing bodies take their glow colour brightened
// by the luminance of their texture.
const vec3 FORCE_SIGHT_LUMINANCE = vec3(0.30, 0.59, 0.11);
const float FORCE_SIGHT_GREY = 0.65;
const float FORCE_SIGHT_GLOW_ALPHA = 0.8;

vec3 forceSightGrey(vec3 litColor) {
    return vec3(dot(litColor, FORCE_SIGHT_LUMINANCE) * FORCE_SIGHT_GREY);
}

// The glow colour to add over the scene, already weighted by its alpha and
// kept in range.
vec3 forceSightGlow(vec3 texColor, vec4 glow) {
    return clamp(glow.rgb * (glow.a + dot(texColor, FORCE_SIGHT_LUMINANCE)), 0.0, 1.0);
}
