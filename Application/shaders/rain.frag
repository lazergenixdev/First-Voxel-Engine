#version 450 core

layout (location = 0) in float fade;
layout (location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(vec3(0.9, 0.9, 1.0), fade*0.2);
}
