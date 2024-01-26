#version 450 core

layout (location = 0) in vec4 v_position;
layout (location = 0) out float f_fade;

layout (push_constant) uniform A {
	mat4 view_projection;
} transform;

void main() {
	vec3 position = v_position.xyz;
	f_fade = v_position.w;
	gl_Position = transform.view_projection * vec4(position, 1.0);
}
