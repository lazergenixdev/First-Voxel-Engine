#version 450 core

layout (location = 0) in  vec3 v_position;
layout (location = 0) out vec3 f_position;

layout (push_constant) uniform A {
	vec3 offset;
} chunk;

layout (set = 0, binding = 0) uniform B {
	mat4 view_projection;
} u;

void main() {
	vec3 position = v_position * vec3(1.0, 500.0, 1.0);
	//+ chunk.offset*128.0;
	f_position = position;
	gl_Position = u.view_projection * vec4(position, 1.0);
}
