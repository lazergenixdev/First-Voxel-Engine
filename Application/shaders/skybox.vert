#version 450 core

layout (location = 0) out vec3 direction;

layout (push_constant) uniform A {
	mat4 view_projection;
} transform;

/*
  0 -- 1
  |\   |\
  | 4 -- 5
  | |  | |
  2-|- 3 |
   \|   \|
    6 -- 7
  
  z y
   \|
    *- x
*/

const vec3[8] cube_vertices = vec3[8] (
	vec3(-1, 1, 1),
	vec3( 1, 1, 1),
	vec3(-1,-1, 1),
	vec3( 1,-1, 1),
	vec3(-1, 1,-1),
	vec3( 1, 1,-1),
	vec3(-1,-1,-1),
	vec3( 1,-1,-1)
);

const uint[36] cube_indices = uint[36] (
	0, 3, 1,
	0, 2, 3, // Z+
	1, 4, 0,
	1, 5, 4, // Y+
	0, 6, 2,
	0, 4, 6, // X-
	1, 7, 5,
	1, 3, 7, // X+
	2, 7, 3,
	2, 6, 7, // Y-
	4, 7, 6,
	4, 5, 7  // Z-
);

void main() {
	uint index = cube_indices[gl_VertexIndex];
	vec3 P = cube_vertices[index];
	direction = P;
	vec4 L = transform.view_projection * vec4(P, 1.0);
	gl_Position = vec4(L.xy, L.w-0.0001, L.w);
}
