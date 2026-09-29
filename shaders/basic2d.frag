// One "uber" shader for all 2D drawing. Output is premultiplied alpha and the
// blend state is (ONE, ONE_MINUS_SRC_ALPHA), so a fragment with alpha 0 but
// non-zero color is additive - that is how glows mix with normal drawing in a
// single batch.
//
// Modes (vModeParam.x):
//   0 textured : texture * color                     (Dear ImGui, sprites, text)
//   1 disc     : SDF disc / ring, uv in [-1,1]        param = inner radius (0 = filled)
//   2 glow     : additive radial falloff              param = falloff exponent
//   3 line     : antialiased band, uv.y in [-1,1]

#ifdef VULKAN
#define VARYING(n) layout(location = n)
layout(set = 0, binding = 0) uniform sampler2D uTexture;
#else
#define VARYING(n)
uniform sampler2D uTexture;
#endif

VARYING(0) in vec2 vUV;
VARYING(1) in vec4 vColor;
VARYING(2) flat in vec2 vModeParam;

layout(location = 0) out vec4 outColor;

void main() {
    // Derivatives and the texture fetch happen in uniform control flow.
    vec4 texel = texture(uTexture, vUV);
    float dist = length(vUV);
    float distWidth = max(fwidth(dist), 1e-4);
    float across = abs(vUV.y);
    float acrossWidth = max(fwidth(vUV.y), 1e-4);

    int mode = int(vModeParam.x + 0.5);
    float param = vModeParam.y;

    if (mode == 0) {
        vec4 c = texel * vColor;
        outColor = vec4(c.rgb * c.a, c.a);
    } else if (mode == 1) {
        float outer = 1.0 - smoothstep(1.0 - distWidth, 1.0, dist);
        float inner = param > 0.0 ? smoothstep(param - distWidth, param, dist) : 1.0;
        float a = vColor.a * outer * inner;
        outColor = vec4(vColor.rgb * a, a);
    } else if (mode == 2) {
        float falloff = pow(clamp(1.0 - dist, 0.0, 1.0), max(param, 0.01));
        outColor = vec4(vColor.rgb * vColor.a * falloff, 0.0);
    } else {
        float coverage = 1.0 - smoothstep(1.0 - acrossWidth * 1.5, 1.0, across);
        float a = vColor.a * coverage;
        outColor = vec4(vColor.rgb * a, a);
    }
}
