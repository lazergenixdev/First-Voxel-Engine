#version 450 core

layout (location = 0) in  vec3 position;
layout (location = 1) in  vec3 color;
layout (location = 0) out vec4 fragColor;

void main() {
//  fragColor = vec4(normal, 1.0);
//  fragColor = vec4(color * dot(normal,vec3(0.0,1.0,0.0)), 1.0);
//  fragColor = vec4(color, 1.0);
#if 0
    vec3 normal = normalize(cross(dFdx(position), dFdy(position)));
    fragColor = vec4(vec3(dot(normal,vec3(0.0,1.0,0.0))), 1.0);
#else
    fragColor = vec4(1.0);
#endif
}
