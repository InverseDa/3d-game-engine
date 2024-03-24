#version 460

layout(push_constant) uniform Push {
    vec3 color;
} push;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(push.color, 1.0);
}