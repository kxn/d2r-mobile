#version 450
// Owned functional oracle for input ordering, formats and render-pass keys.
layout(input_attachment_index=0,set=0,binding=0) uniform subpassInput first;
layout(input_attachment_index=1,set=0,binding=1) uniform subpassInput second;
layout(input_attachment_index=2,set=0,binding=2) uniform subpassInput third;
layout(location=0) out vec4 color;
void main() {
    color=vec4(subpassLoad(first).r,subpassLoad(second).g,subpassLoad(third).b,1.0);
}
