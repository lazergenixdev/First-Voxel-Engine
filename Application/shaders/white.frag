#version 450 core

layout (location = 0) in vec4 position;
layout (location = 1) in vec2 uv;
layout (location = 2) in flat uint normal_index;
layout (location = 0) out vec4 fragColor;

layout (push_constant) uniform A {
    layout (offset = 80)
    vec3 light_direction;
    vec3 eye_position; 
} u;

layout (set = 0, binding = 0) uniform sampler2D atlas;
layout (set = 1, binding = 0) uniform usampler3D block_types;

void main() {
//    fragColor = vec4(fract(uv), 1.0, 1.0);
    vec4 final_color = vec4(vec3(1.0), 1.0);
    float fog_blend = 0.005*length(u.eye_position - position.xyz);
    fragColor = mix(final_color, vec4(0.01), max(min(fog_blend,1.0),0.0));
}
