#version 450 core

layout (location = 0) in vec2 uv;
layout (location = 0) out vec4 fragColor;

layout (set = 0, binding = 0) uniform sampler2D depth;

//const vec2 resolution = vec2(1280.0, 720.0);
const vec2 resolution = vec2(1920.0, 1080.0);

float getPixelDepth(int x, int y) {
    float d = 6.0 * texture(depth, uv + vec2(float(x)/resolution.x, float(y)/resolution.y)).r;
    return pow(d,4.0);
}

layout (push_constant) uniform A {
    vec3 color;
} p;

float diff1() {
    float d = getPixelDepth(0, 0);
    float depthDiff = 0.0;

    depthDiff += abs(d - getPixelDepth(1, 0));
    depthDiff += abs(d - getPixelDepth(-1,0));
    depthDiff += abs(d - getPixelDepth(0, 1));
    depthDiff += abs(d - getPixelDepth(0,-1));

    return depthDiff;
}

float diff2() {
    float d = getPixelDepth(0, 0);
    float s = 0.0;

    float d1 = d - getPixelDepth( 1,0);
    float d2 = d - getPixelDepth(-1,0);

//    if (abs(d1 - d2) < .5) return 0.0;

    s += abs(d1) + abs(d2);

    float d3 = d - getPixelDepth(0, 1);
    float d4 = d - getPixelDepth(0,-1);

    if ((abs(d3 - d4) < .5) && (abs(d1 - d2) < .5)) return 0.0;

    s += abs(d3) + abs(d4);

    return s * 16.0;
}

void main() {
    float d = diff1();

//  fragColor = vec4(vec3(1.0), smoothstep(0.001, 0.01, d));
    fragColor = vec4(p.color, d*1.0);
}
