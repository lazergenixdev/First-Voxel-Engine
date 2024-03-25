#version 450 core

layout (location = 0) in  vec3 position;
layout (location = 1) in  vec3 color;
layout (location = 0) out vec4 fragColor;

layout (set = 0, binding = 0) uniform B {
	mat4 view_projection;
	ivec4 position;
} u;

vec3 pal( in float t, in vec3 a, in vec3 b, in vec3 c, in vec3 d ) {
    return a + b*cos( 6.28318*(c*t+d) );
}
#define saturate(X) min(max(X, 0.0), 1.0)
//#define COLOR(X) pal(X, vec3(0.5,0.5,0.5),vec3(0.5,0.5,0.5),vec3(1.0,1.0,1.0),vec3(0.0,0.33,0.67))
//#define COLOR(X) pow(vec3(.1, .7, .8), vec3(4.*saturate(1.3-X)))
#define COLOR(X) pow(vec3(.1, .7, .8), vec3(-1e-4*(X)))

#define RANGE(MIN,MAX, CA, CB) \
if (MIN < y && y <= MAX) return vec4(mix(CA, CB, (y - MIN)/(MAX - MIN)), 1.0)

#define COLOR_UNDERWATER 0
#define H_SNOW      21000.0
#define H_MOUNTAIN  20000.0
#define H_HILL      10000.0
#define H_GRASS     3000.0
#define H_SEALEVEL -3000.0
vec4 col_terrain(float y) {
    vec4 r = vec4(0.2,0.175,0.125,1.0);
    if (y > 21000.0) return vec4(1.0);
    RANGE( H_MOUNTAIN,  H_SNOW,     vec3(0.4,0.4,0.1),     vec3(1.0,1.0,1.0));
    RANGE( H_HILL,      H_MOUNTAIN, vec3(0.2,0.2,0.0),     vec3(0.4,0.4,0.1));
    RANGE( H_GRASS,     H_HILL,     vec3(0.0,0.6,0.0),     vec3(0.2,0.2,0.0));
    RANGE( H_SEALEVEL,  H_GRASS,    vec3(0.0,0.3,0.0),     vec3(0.0,0.6,0.0));
    RANGE( -3500.0,     H_SEALEVEL, vec3(0.8,0.7,0.5),     vec3(0.0,0.3,0.0));
#if COLOR_UNDERWATER
    return vec4(0.0,0.0,0.6,1.0);
#else
    RANGE( -20000.0,    -3500.0,    vec3(0.2,0.175,0.125), vec3(0.8,0.7,0.5));
    return r;
#endif
}

//#define R 2048.0
#define R 256.0

void main() {
//  vec3 normal = normalize(cross(dFdx(position), dFdy(position)));

//  fragColor = vec4(normal, 1.0);
//  fragColor = vec4(color * dot(normal,vec3(0.0,1.0,0.0)), 1.0);
//  fragColor = vec4(color, 1.0);

    fragColor = col_terrain(ceil(position.y) + u.position.y) * color.x;
//  fragColor = vec4(COLOR(floor(position.y-(R*2))/R) * color, 1.0);
//  float F = dot(normal,vec3(0.0,1.0,0.0));
//  fragColor = vec4(COLOR(floor(position.y)/64.0), 1.0) * max(0.6,F);

#if 0
    fragColor = vec4(vec3(dot(normal,vec3(0.0,1.0,0.0))), 1.0);
#elif 0
    vec3 h = vec3(position.y*0.2 + 0.5);
    fragColor = vec4(h*max(u.normal,dot(normal,vec3(0.0,1.0,0.0))), 1.0);
#elif 0
    fragColor = vec4(color.x);
#endif
}
