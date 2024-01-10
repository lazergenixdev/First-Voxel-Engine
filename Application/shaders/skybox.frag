#version 450 core

layout (location = 0) in  vec3 direction;
layout (location = 0) out vec4 fragColor;

layout (set = 0, binding = 0) uniform samplerCube gCubemapTexture;

void main() {
	vec4 color = texture(gCubemapTexture, direction);
//	float r = pow(color.r, 1.5);
//	float g = pow(color.g, 1.5);
//	float b = pow(color.b, 1.5);
//	fragColor = vec4(r, g, b, 1.0);
	fragColor = vec4(color.rgb, 1.0);
}
