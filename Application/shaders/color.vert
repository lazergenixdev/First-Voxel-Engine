#version 450 core

layout (location = 0) in  vec3 v_position;
layout (location = 0) out vec3 f_position;
layout (location = 1) out flat vec3 f_color;

layout (push_constant) uniform A {
	mat4 view_projection;
	float normal;
} transform;

vec3[8] color_map = vec3[8] (
	vec3(1.0, 0.0, 0.0),
	vec3(0.0, 1.0, 0.0),
	vec3(0.0, 0.0, 1.0),
	vec3(0.0, 1.0, 1.0),
	vec3(1.0, 0.0, 1.0),
	vec3(1.0, 1.0, 0.0),
	vec3(0.5, 0.5, 0.5),
	vec3(1.0, 1.0, 1.0)
);

void main() {
	int i = gl_VertexIndex/(33*33);
	vec3 position = v_position;
	f_position = v_position;
	f_color = color_map[i%8];
	gl_Position = transform.view_projection * vec4(position, 1.0);
}
