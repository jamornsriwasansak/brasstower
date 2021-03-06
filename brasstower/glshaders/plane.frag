#version 450 core

layout(location = 0) out vec4 color;

in vec4 vPosition;
in vec4 vShadowCoord;

uniform vec3 uCameraPos;
uniform sampler2D uShadowMap;

uniform vec3 uPlaneNormal;

uniform vec3 uLightPosition;
uniform vec3 uLightDir;
uniform vec3 uLightIntensity;
uniform vec2 uLightThetaMinMax;

vec2 poissonDisk[4] = vec2[](
	vec2( -0.94201624, -0.39906216 ),
 	vec2( 0.94558609, -0.76890725 ),
 	vec2( -0.094184101, -0.92938870 ),
 	vec2( 0.34495938, 0.29387760 )
);

vec3 shadeSpotlight(vec3 position)
{
	vec3 diff = uLightPosition - position;
	float dist2 = dot(diff, diff);
	float dist = sqrt(dist2);
	vec3 nDiff = diff / dist;

	float cos1 = max(dot(nDiff, uPlaneNormal), 0.f);
	float cos2 = max(-dot(nDiff, uLightDir), 0.f);
	float spotlightScale = 1.0f - smoothstep(uLightThetaMinMax.x, uLightThetaMinMax.y, acos(cos2));
	return min(cos1 * spotlightScale / dist2, 0.01f) * 10.f * uLightIntensity;
}

const vec3 baseColor[2] = vec3[](
	vec3(86.0f / 256.0f, 86.0f / 256.0f, 86.0f / 256.0f),
	vec3(86.0f / 256.0f, 86.0f / 256.0f, 86.0f / 256.0f) * 1.2f
);

void main()
{
	// discretize position
	ivec4 discretized = ivec4(ceil(vPosition));
	// grid code
	int code = (discretized.x + discretized.y + discretized.z) % 2;
	// map code to diff reflectance
	vec3 diffuseReflectance = baseColor[code];

	vec3 projShadowCoord = vShadowCoord.xyz / vShadowCoord.w * 0.5f + 0.5f;
	float shadowColor = 0.0f;
	for (int i = 0;i < 4;i++)
	{
		vec2 offset = poissonDisk[i] / 500.0f;
		shadowColor += texture(uShadowMap, projShadowCoord.xy + offset).r > vShadowCoord.z ? 1.0f : 0.0f;
	}
	shadowColor *= 0.25f;

	vec3 diffuse = diffuseReflectance * shadeSpotlight(vec3(vPosition)) * vec3(1.0f - shadowColor);
	vec3 ambient = diffuseReflectance * 0.05f;
	color = vec4(diffuse + ambient, 1.0f);
	color = pow(color, vec4(1.0/2.2));
}