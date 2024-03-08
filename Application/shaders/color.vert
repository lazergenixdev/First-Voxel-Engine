#version 450 core

layout (location = 0) in  vec4 v_position;
layout (location = 0) out vec3 f_position;
layout (location = 1) out flat vec3 f_color;

layout (push_constant) uniform A {
	vec3 offset;
} chunk;

layout (set = 0, binding = 0) uniform B {
	mat4 view_projection;
	float time;
} u;

#define COLOR(X) pow(vec3(.1, .7, .8), vec3(4.*X))

vec3[8] color_map = vec3[8] (
	vec3(1.0,0.0,0.0),
	vec3(0.0,1.0,0.0),
	vec3(0.0,0.0,1.0),
	vec3(0.0,1.0,1.0),
	vec3(1.0,0.0,1.0),
	vec3(1.0,1.0,0.0),
	vec3(1.0,1.0,1.0),
	vec3(0.5,0.5,0.5)
);

vec3 pal( in float t, in vec3 a, in vec3 b, in vec3 c, in vec3 d ) {
    return a + b*cos( 6.28318*(c*t+d) );
}

float wave(vec2 p) {
	return 20.0 * (sin(p.x/128.0 + u.time) + sin(p.y/128.0 + u.time));
}

void main() {
	vec3 position = v_position.xyz + chunk.offset;
	//position += vec3(0.0, wave(position.xz), 0.0);
	f_position = position;
	//int i = gl_VertexIndex/(4);
	//f_color = color_map[i%8];
    //f_color = pal(v_position.w, vec3(0.5,0.5,0.5),vec3(0.5,0.5,0.5),vec3(1.0,1.0,1.0),vec3(0.0,0.33,0.67));
	//f_color = COLOR(v_position.w);
	f_color = vec3(v_position.w);
	gl_Position = u.view_projection * vec4(position, 1.0);
}
