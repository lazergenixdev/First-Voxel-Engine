#version 450 core

layout (location = 0) in  vec3 v_position;
layout (location = 1) in  vec3 v_texcoord;
layout (location = 0) out vec4 out_position;
layout (location = 1) out vec2 out_texcoord;
layout (location = 2) out flat uint out_normal_index;

layout (push_constant) uniform A {
	mat4 view_projection;
	vec3 chunk_position;
} transform;

void main() {
	vec3 position = v_position + transform.chunk_position * 8.0;
	gl_Position = transform.view_projection * vec4(position, 1.0);
	out_position = vec4(position, gl_Position.z/100.0);
	out_texcoord = v_texcoord.xy;
	out_normal_index = uint(v_texcoord.z);
}
