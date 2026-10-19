#version 450
// Positive-height Vulkan viewport: texel coordinates and framebuffer rows
// have the same orientation as the reference full-size image blit.
void main() {
    vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
