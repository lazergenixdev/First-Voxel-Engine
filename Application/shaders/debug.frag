#version 450 core

layout (location = 0) in  vec3 position;
layout (location = 0) out vec4 frag_color;

float dist(float y) {
	return abs(y/400.0);
}

void main() {
	float d = max(0.0, 1.0 - dist(position.y));
	frag_color = vec4(1.0, 0.0, 0.0, d);
}
