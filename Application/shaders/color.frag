#version 450 core
#include "../src/config.hpp"

layout (location = 0) in  vec4 in_position;
layout (location = 1) in  vec2 uv;
layout (location = 2) in flat uint normal_index;
layout (location = 0) out vec4 fragColor;

layout (push_constant) uniform A {
    layout (offset = 80)
    vec3 light_direction; 
    vec3 eye_position;
} u;

layout (set = 0, binding = 0) uniform sampler2D atlas;
layout (set = 1, binding = 0) uniform usampler3D block_types;

vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

const vec3[6] normal_map = vec3[6] (
    vec3( 1.0, 0.0, 0.0),
    vec3( 0.0, 1.0, 0.0),
    vec3( 0.0, 0.0, 1.0),
    vec3(-1.0, 0.0, 0.0),
    vec3( 0.0,-1.0, 0.0),
    vec3( 0.0, 0.0,-1.0)
);
const vec2[6] atlas_offsets = vec2[6] (
    vec2((6.0 * 16.0)/128.0, (0.0 * 16.0)/96.0),
    vec2((6.0 * 16.0)/128.0, (2.0 * 16.0)/96.0),
    vec2((6.0 * 16.0)/128.0, (1.0 * 16.0)/96.0),
    vec2((6.0 * 16.0)/128.0, (3.0 * 16.0)/96.0),
    vec2((6.0 * 16.0)/128.0, (5.0 * 16.0)/96.0),
    vec2((6.0 * 16.0)/128.0, (4.0 * 16.0)/96.0)
);
const vec2 atlas_size = vec2(16.0) / vec2(128.0,96.0);

void main() {
#if 0
    fragColor = vec4(vec3(1.0)-in_position.xyz/vec2(48.0*8.0, 16.0*8.0).xyx, 1.0);
#elif 0
    float a = atan(6.28318530718*0.5 + in_position.x / in_position.z) / (6.28318530718/4.0);
    vec3 normal = normal_map[normal_index];
    vec3 inside = in_position.xyz - normal * 0.5;
    vec3 object_color = hsv2rgb(vec3(a, 1.0, inside.y/64.0));
    fragColor = vec4(object_color, 1.0);
#elif 1
    vec3 normal = normal_map[normal_index];
    float diffuse = max(0.0,dot(-u.light_direction, normal));
    vec3 inside = in_position.xyz - normal * 0.5;
    inside.x = 0; inside.z = 0; // TODO: delete me
    uint index = texelFetch(block_types, ivec3(inside), 0).r & 7;
   //index = 6;
    vec3 object_color = texture(atlas, vec2(float(index) * 16.0/128.0, atlas_offsets[normal_index].y) + fract(uv) * atlas_size).rgb;
    float ambient = 0.05;
    
    float specular = 0.0;
    vec3 VertexToEye = normalize(u.eye_position - in_position.xyz);
    vec3 LightReflect = normalize(reflect(u.light_direction, normal));
    float SpecularFactor = dot(VertexToEye, LightReflect);
    if (SpecularFactor > 0) {
        SpecularFactor = pow(SpecularFactor, 32.0);
        specular = 1.0 * SpecularFactor;
    }

#if RAIN
    vec4 final_color = vec4((vec3(diffuse + ambient) + specular * vec3(0.0, 1.0, 1.0)) * object_color, 1.0);
    float fog_blend = 0.01*length(u.eye_position - in_position.xyz);
    fragColor = mix(final_color, vec4(0.1), max(min(fog_blend,1.0),0.0));
#else
    fragColor = vec4((vec3(diffuse + ambient) + specular * vec3(0.0, 1.0, 1.0)) * object_color, 1.0);
#endif

//    fragColor = vec4(normal, 1.0);
#elif 0
    vec3 normal = normal_map[normal_index];
    float diffuse = 1.0;//max(0.0,dot(-u.light_direction, normal));
    vec3 inside = in_position.xyz - normal * 0.5;
    vec3 object_color = mix(vec3(1.0), vec3(0.0), 1.0 - inside.y/64.0);
    float ambient = 0.05;
    fragColor = vec4(vec3(diffuse + ambient) * object_color, 1.0);
#elif 0
    fragColor = vec4(vec3(0.0), 1.0);
#endif
}
