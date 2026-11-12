#version 450
layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, std140) uniform Lighting {
    vec4 lightDirection;
    vec4 lightColorIntensity;
    vec4 ambientColorIntensity;
    vec4 specularColorShininess;
    vec4 cameraPosition;
    vec4 pointPosition;
    vec4 pointColorIntensity;
} lighting;

struct PointLight { vec4 position; vec4 colorIntensity; };
layout(set = 2, binding = 0, std430) readonly buffer PointLights {
    uint count; uint p0; uint p1; uint p2;
    PointLight lights[];
} pointLights;

struct SpotLight { vec4 position; vec4 direction; vec4 colorIntensity; vec4 angles; };
layout(set = 3, binding = 0, std430) readonly buffer SpotLights {
    uint count; uint p0; uint p1; uint p2;
    SpotLight lights[];
} spotLights;

layout(set = 4, binding = 0) uniform sampler2D albedoTex;

void main() {
vec3 albedo = texture(albedoTex, fragUV).rgb;
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(lighting.cameraPosition.xyz - fragPosition);
    float shininess = max(lighting.specularColorShininess.a, 1.0);

    vec3 ambient = lighting.ambientColorIntensity.rgb * lighting.ambientColorIntensity.a * albedo;

    vec3 dirL = normalize(lighting.lightDirection.xyz);
    float dD = max(dot(normal, dirL), 0.0);
    vec3 dH = dirL + viewDir;
    float dS = (dD > 0.0 && dot(dH,dH) > 1e-6) ? pow(max(dot(normal, normalize(dH)), 0.0), shininess) : 0.0;
    vec3 directional = lighting.lightColorIntensity.rgb * lighting.lightColorIntensity.a *
                       (dD * albedo + dS * lighting.specularColorShininess.rgb);

    vec3 pointL = vec3(0.0);
    for (uint i = 0; i < min(pointLights.count, 8u); ++i) {
        vec3 toP = pointLights.lights[i].position.xyz - fragPosition;
        float d2 = max(dot(toP,toP), 0.01);
        vec3 L = toP * inversesqrt(d2);
        float diff = max(dot(normal, L), 0.0);
        vec3 H = L + viewDir;
        float spec = (diff>0.0 && dot(H,H)>1e-6) ? pow(max(dot(normal,normalize(H)),0.0), shininess) : 0.0;
        pointL += pointLights.lights[i].colorIntensity.rgb * pointLights.lights[i].colorIntensity.a / d2 *
                  (diff * albedo + spec * lighting.specularColorShininess.rgb);
    }

    vec3 spotL = vec3(0.0);
    for (uint i = 0; i < min(spotLights.count, 8u); ++i) {
        vec3 fromS = fragPosition - spotLights.lights[i].position.xyz;
        float d2 = max(dot(fromS,fromS), 0.01);
        vec3 R = fromS * inversesqrt(d2);
        vec3 dir = normalize(spotLights.lights[i].direction.xyz);
        float innerCos = cos(radians(spotLights.lights[i].angles.x));
        float outerCos = cos(radians(spotLights.lights[i].angles.y));
        float cone = smoothstep(outerCos, innerCos, dot(R, dir));
        vec3 toSurf = -R;
        float diff = max(dot(normal, toSurf), 0.0);
        vec3 H = toSurf + viewDir;
        float spec = (diff>0.0 && dot(H,H)>1e-6) ? pow(max(dot(normal,normalize(H)),0.0), shininess) : 0.0;
        spotL += cone * spotLights.lights[i].colorIntensity.rgb * spotLights.lights[i].colorIntensity.a / d2 *
                 (diff * albedo + spec * lighting.specularColorShininess.rgb);
    }

    outColor = vec4(ambient + directional + pointL + spotL, 1.0);
}