#version 440 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

layout(binding = 0) uniform sampler2D u_displacement;
layout(binding = 1) uniform sampler2D u_normal;

out vec2 textureCoord;
out vec3 verNormal;
out vec3 FragPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
	vec3 position = texture(u_displacement,aTexCoord).rgb;
	vec3 normal = texture(u_normal,aTexCoord).rgb;

	FragPos = vec3(model * vec4(position, 1.0f));
	gl_Position = projection * view *  model * vec4(position, 1.0f);
    verNormal = mat3(transpose(inverse(model))) * normal;
}

 