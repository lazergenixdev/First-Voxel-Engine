#version 450 core

layout (location = 0) in  vec3 position;
layout (location = 1) in  vec3 color;
layout (location = 0) out vec4 fragColor;

layout (push_constant) uniform A {
	mat4 view_projection;
	float normal;
} u;

vec3 pal( in float t, in vec3 a, in vec3 b, in vec3 c, in vec3 d ) {
    return a + b*cos( 6.28318*(c*t+d) );
}
#define saturate(X) min(max(X, 0.0), 1.0)
//#define COLOR(X) pal(X, vec3(0.5,0.5,0.5),vec3(0.5,0.5,0.5),vec3(1.0,1.0,1.0),vec3(0.0,0.33,0.67))
//#define COLOR(X) pow(vec3(.1, .7, .8), vec3(4.*saturate(1.3-X)))
#define COLOR(X) pow(vec3(.1, .7, .8), vec3(4.*saturate(0.3-(0.5*X))))

void main() {
//  vec3 normal = normalize(cross(dFdx(position), dFdy(position)));

//  fragColor = vec4(normal, 1.0);
//  fragColor = vec4(color * dot(normal,vec3(0.0,1.0,0.0)), 1.0);
//  fragColor = vec4(color, 1.0);

    fragColor = vec4(COLOR(floor(position.y)/64.0) * color, 1.0);
//  float F = dot(normal,vec3(0.0,1.0,0.0));
//  fragColor = vec4(COLOR(floor(position.y)/64.0), 1.0) * max(0.6,F);

#if 0
    fragColor = vec4(vec3(dot(normal,vec3(0.0,1.0,0.0))), 1.0);
#elif 0
    vec3 h = vec3(position.y*0.2 + 0.5);
    fragColor = vec4(h*max(u.normal,dot(normal,vec3(0.0,1.0,0.0))), 1.0);
#elif 0
    fragColor = vec4(1.0);
#endif
}
