#version 440 core

in vec2 textureCoord;
in vec3 verNormal;
in vec3 FragPos;

out vec4 FragColor;

struct Light {
    vec3 position;
    //vec3 direction;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

uniform vec3 viewPos;
uniform Light mLight;

void main()
{
//    //环境光
//    vec3 ambient = mLight.ambient * 0.6;
//  	
//    //漫反射
//    vec3 norm = normalize(verNormal);
//    vec3 lightDir = normalize(mLight.position - FragPos);
//    //vec3 lightDir = normalize(-mLight.direction);
//    float diff = max(dot(norm, lightDir), 0.0);
//    vec3 diffuse = diff * mLight.diffuse;  
//    
//    //Blinn-Phong镜面光
//    vec3 viewDir = normalize(viewPos - FragPos);
//    vec3 halfwayDir = normalize(lightDir + viewDir);  
//    float spec = pow(max(dot(norm, halfwayDir), 0.0), 32.0);
//    //vec3 reflectDir = reflect(-lightDir, norm);  
//    //float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
//    vec3 specular = 0.5 * spec * mLight.specular;
//
//    //菲涅尔
//    float R0 = 0.02;
//    float cosTheta = dot(viewDir, -norm);
//    float R = R0 + (1 - R0) * pow(1 - cosTheta, 5.0);
//    vec3 fresnel = vec3(R);
//        
//    vec3 re = (ambient + diffuse + specular) * vec4(0.0313, 0.43, 0.815, 1.0).xyz;
//    re += fresnel * mLight.ambient * 0.8;
//    FragColor = vec4(re, 1.0);

    //////////////////////////////////////////////////////////////////////////////////////////////

	vec3 norm = normalize(verNormal);
	vec3 lightDir = normalize(mLight.position - FragPos); 
	vec3 viewDir = normalize(viewPos - FragPos);
	
	float heightMax = 40.0;
	float heightMin = -40.0;

	vec3 ambientFactor = vec3(0.0);
	vec3 diffuseFactor = vec3(1.0);
	
	vec3 skyColor = vec3(0.65, 0.80, 0.95);
	
	if (dot(norm, viewDir) < 0) norm = -norm;
	
    // Ambient
    vec3 ambient = mLight.ambient * ambientFactor;
	
	// Height Color
	vec3 shallowColor = vec3(0.0, 0.64, 0.68);
	vec3 deepColor = vec3(0.02, 0.05, 0.10);
	
	float relativeHeight;	// from 0 to 1
	relativeHeight = (FragPos.y - heightMin) / (heightMax - heightMin);
	vec3 heightColor = relativeHeight * shallowColor + (1 - relativeHeight) * deepColor;
	// heightColor = vec3(s);	// Black and white
	
	// Spray
	float sprayThresholdUpper = 1.0;
	float sprayThresholdLower = 0.9;
	float sprayRatio = 0;
	if (relativeHeight > sprayThresholdLower) sprayRatio = (relativeHeight - sprayThresholdLower) / (sprayThresholdUpper - sprayThresholdLower);
	vec3 sprayBaseColor = vec3(1.0);
	vec3 sprayColor = sprayRatio * sprayBaseColor;	
	
    // Diffuse  	
	float diff = max(dot(norm, lightDir), 0.0);
	vec3 diffuse = diffuseFactor * mLight.diffuse * diff;
	diffuse = vec3(0.0);
	
	// Psudo reflect
	float refCoeff = pow(max(dot(norm, viewDir), 0.0), 0.3);	// Smaller power will have more concentrated reflect.
	vec3 reflectColor = (1 - refCoeff) * skyColor;
	
    // Specular
	//vec3 halfwayDir = normalize(lightDir + viewDir);
    //float spec = pow(max(dot(norm, halfwayDir), 0.0), 64.0);
	vec3 reflectDir = reflect(-lightDir, norm); 
	float specCoeff = pow(max(dot(viewDir, reflectDir), 0.0), 64) * 3;	// Over exposure
    vec3 specular = mLight.specular * specCoeff;
        
	vec3 combinedColor = ambient + diffuse + heightColor + reflectColor;    
	
	//sprayRatio = clamp(sprayRatio, 0, 1);
	//combinedColor *= (1 - sprayRatio);
	//combinedColor += sprayColor;
	
	specCoeff = clamp(specCoeff, 0, 1);
	combinedColor *= (1 - specCoeff);
	combinedColor += specular;	
	FragColor = vec4(combinedColor, 1.0f); 
	
	//color = vec4(sprayColor, 1.0f); 
	//color = vec4(heightColor, 1.0f); 
	//color = vec4(specular, 1.0f); 
	//color = vec4(1.0, 1.0, 1.0, 1.0);
}