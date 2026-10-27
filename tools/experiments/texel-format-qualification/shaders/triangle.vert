#version 450
void main() {
  vec2 xy=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3))[gl_VertexIndex];
  gl_Position=vec4(xy,0,1);
}
