const int LIGHT_DYNAMIC_TYPE_ALL = 1;
const int LIGHT_DYNAMIC_TYPE_DYNAMIC_ONLY = 2;

// Smooth reach: the quadratic falloff alone still carries a quarter of its
// strength at the radius, so a light that stops there leaves a hard-edged
// disc on the floor. The window brings it to exactly zero over the outer 40%
// of the radius and leaves the inner 60% untouched.
float lightAttenuationQuadratic(Light light, float distance) {
    float reach = 1.0 - smoothstep(0.6, 1.0, distance / max(1e-4, light.radius));
    return reach * pow(light.radius, 2.0) / pow(light.radius + distance, 2.0);
}

// Dynamic objects: at most MAX_OBJECT_LIGHTS lights chosen per object, falling
// off as 1 / (1 + 4.5 (d / r)^2) with no cut-off at the radius.
float objectLightAttenuation(float radius, float distance) {
    float rel = distance / max(0.1, radius);
    return 1.0 / (1.0 + 4.5 * rel * rel);
}
