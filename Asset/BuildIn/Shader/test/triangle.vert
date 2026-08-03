#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

layout (location = 0) out vec2 OUT_UV;

void main() 
{
    vec2 position;
    if(gl_VertexIndex == 0) position = vec2(0.5f, 0.2f);
    if(gl_VertexIndex == 1) position = vec2(0.2f, 0.7f);
    if(gl_VertexIndex == 2) position = vec2(0.8f, 0.7f);

	OUT_UV 				= position; 	
	gl_Position 		= vec4(OUT_UV * 2.0f - 1.0f, 0.0f, 1.0f);             

	// OUT_UV 		    = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); 	//[0, 0]	~	[2, 2]
	// gl_Position 		= vec4(OUT_UV * 2.0f - 1.0f, 0.0f, 1.0f);               //[-1, -1]	~	[3, 3], [1, 1]之外被裁剪，总共只有一个三角形
}
