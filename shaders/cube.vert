#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 1, binding = 0, std140) uniform ModelUBO {
    mat4 model;
    mat4 mvp;
} ubo;

layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;

void main() {
    vec4 world = ubo.model * vec4(inPosition, 1.0);
    fragPosition = world.xyz;
    mat3 nm = transpose(inverse(mat3(ubo.model)));
    fragNormal = normalize(nm * inNormal);
    fragUV = inUV;
    gl_Position = ubo.mvp * vec4(inPosition, 1.0);
}