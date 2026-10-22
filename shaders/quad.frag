#version 450
layout(set=0,binding=0) uniform sampler2D sourceTexture;
layout(location=0) in vec2 fragmentUv;
layout(location=1) in vec4 fragmentColor;
layout(location=0) out vec4 outputColor;
void main() { outputColor = texture(sourceTexture, fragmentUv) * fragmentColor; }
