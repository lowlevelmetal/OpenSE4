// Shared by the Vulkan (SPIR-V via glslc, VULKAN defined) and OpenGL 3.3
// (compiled at runtime with a "#version 330 core" prologue) backends.
// No #version line here on purpose - see cmake/Shaders.cmake.

#ifdef VULKAN
#define VARYING(n) layout(location = n)
layout(push_constant) uniform PushConstants { mat4 uTransform; };
#else
#define VARYING(n)
uniform mat4 uTransform;
#endif

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aModeParam;

VARYING(0) out vec2 vUV;
VARYING(1) out vec4 vColor;
VARYING(2) flat out vec2 vModeParam;

void main() {
    vUV = aUV;
    vColor = aColor;
    vModeParam = aModeParam;
    gl_Position = uTransform * vec4(aPos, 0.0, 1.0);
}
