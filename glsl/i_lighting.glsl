const int LIGHT_DYNAMIC_TYPE_ALL = 1;
const int LIGHT_DYNAMIC_TYPE_DYNAMIC_ONLY = 2;

float lightAttenuationQuadratic(Light light, float distance) {
    return pow(light.radius, 2.0) / pow(light.radius + distance, 2.0);
}

// Dynamic objects: at most MAX_OBJECT_LIGHTS lights chosen per object, falling
// off as 1 / (1 + 4.5 (d / r)^2) with no cut-off at the radius.
float objectLightAttenuation(float radius, float distance) {
    float rel = distance / max(0.1, radius);
    return 1.0 / (1.0 + 4.5 * rel * rel);
}
