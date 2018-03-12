#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <cuda_gl_interop.h>

#include "cuda/helper.cuh"
#include "mesh.h"

struct Camera
{
	static inline glm::vec3 SphericalToWorld(const glm::vec2 & thetaPhi)
	{
		const float & phi = thetaPhi.x;
		const float & theta = thetaPhi.y;
		const float sinphi = std::sin(phi);
		const float cosphi = std::cos(phi);
		const float sintheta = std::sin(theta);
		const float costheta = std::cos(theta);
		return glm::vec3(costheta * sinphi, cosphi, sintheta * sinphi);
	}

	static inline glm::vec2 WorldToSpherical(const glm::vec3 & pos)
	{
		const float phi = std::atan2(pos.z, pos.x);
		const float numerator = std::sqrt(pos.x * pos.x + pos.z * pos.z);
		const float theta = std::atan2(numerator, pos.y);
		return glm::vec2(theta, phi);
	}

	Camera(const glm::vec3 & pos, const glm::vec3 & lookAt, const float fovy, const float aspectRatio):
		pos(pos),
		dir(glm::normalize(lookAt - pos)),
		thetaPhi(WorldToSpherical(glm::normalize(lookAt - pos))),
		up(glm::vec3(0.0f, 1.0f, 0.0f)),
		fovY(fovy),
		aspectRatio(aspectRatio)
	{}

	void shift(const glm::vec3 & move)
	{
		const glm::vec3 & mBasisZ = dir;
		const glm::vec3 & mBasisX = glm::normalize(glm::cross(up, mBasisZ));
		pos += mBasisZ * move.z + mBasisX * move.x;
	}

	void rotate(const glm::vec2 & rotation)
	{
		thetaPhi += rotation;
		dir = SphericalToWorld(thetaPhi);
	}

	glm::mat4 vpMatrix()
	{
		glm::mat4 viewMatrix = glm::lookAt(pos, dir + pos, glm::vec3(0, 1, 0));
		glm::mat4 projMatrix = glm::perspective(fovY, aspectRatio, 0.01f, 100.0f);
		return projMatrix * viewMatrix;
	}

	float fovY;
	float aspectRatio;
	glm::vec3 pos;
	glm::vec2 thetaPhi;
	glm::vec3 dir;
	glm::vec3 up;
};

static GLFWwindow* InitGL(const size_t width, const size_t height)
{
	if (!glfwInit())
	{
		throw new std::exception("Failed to initialize GLFW\n");
	}

	glfwWindowHint(GLFW_SAMPLES, 1); // 1x antialiasing
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); 
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); 

	GLFWwindow* window; 
	window = glfwCreateWindow(width, height, "Work Please", NULL, NULL);
	if (window == NULL)
	{
		throw new std::exception("Failed to open GLFW window. If you have an Intel GPU, they are not 3.3 compatible. Try the 2.1 version of the tutorials.\n");
	}
	glfwMakeContextCurrent(window); 
	glewExperimental = true;
	if (glewInit() != GLEW_OK)
	{
		throw new std::exception("Failed to initialize GLEW\n");
	}

	glfwSetInputMode(window, GLFW_STICKY_KEYS, GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	return window;
}

// should be singleton
struct ParticleRenderer
{
	const size_t MaxNumParticles = 1000;

	ParticleRenderer(const glm::uvec2 & resolution, const float radius):
		camera(glm::vec3(0, 1, -1), glm::vec3(0), glm::radians(60.0f), (float)resolution.x / (float)resolution.y),
		radius(radius)
	{
		glGenVertexArrays(1, &globalVaoHandle);
		glBindVertexArray(globalVaoHandle);

		// init ssbobuffer and register in cuda
		glGenBuffers(1, &ssboBuffer);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboBuffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, 4 * sizeof(float) * MaxNumParticles, 0, GL_DYNAMIC_COPY);
		checkCudaErrors(cudaGraphicsGLRegisterBuffer(&ssboGraphicsRes, ssboBuffer, cudaGraphicsRegisterFlagsWriteDiscard));

		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glDepthFunc(GL_LEQUAL);

		// load particle mesh
		particleMesh = Mesh::Load("icosphere.objj")[0];
		planeMesh = Mesh::Load("plane.objj")[0];

		initParticleDrawingProgram();
		initInfinitePlaneDrawingProgram();
	}

	std::shared_ptr<OpenglProgram> particlesDrawingProgram;
	std::shared_ptr<OpenglUniform> particlesDrawingProgram_uMVPMatrix;
	std::shared_ptr<OpenglUniform> particlesDrawingProgram_uRadius;
	GLuint particlesDrawingProgram_ssboBinding;
	void initParticleDrawingProgram()
	{
		particlesDrawingProgram = std::make_shared<OpenglProgram>();
		particlesDrawingProgram->attachVertexShader(OpenglVertexShader::CreateFromFile("glshaders/simple.vert"));
		particlesDrawingProgram->attachFragmentShader(OpenglFragmentShader::CreateFromFile("glshaders/simple.frag"));
		particlesDrawingProgram->compile();

		particlesDrawingProgram_uMVPMatrix = particlesDrawingProgram->registerUniform("uMVP");
		particlesDrawingProgram_uRadius = particlesDrawingProgram->registerUniform("uRadius");
		GLuint index = glGetProgramResourceIndex(particlesDrawingProgram->mHandle, GL_SHADER_STORAGE_BLOCK, "ParticlesInfo");
		particlesDrawingProgram_ssboBinding = 0;
		glShaderStorageBlockBinding(particlesDrawingProgram_ssboBinding, index, 0);
	}

	std::shared_ptr<OpenglProgram> planeDrawingProgram;
	std::shared_ptr<OpenglUniform> planeDrawingProgram_uMVPMatrix;
	void initInfinitePlaneDrawingProgram()
	{
		planeDrawingProgram = std::make_shared<OpenglProgram>();
		planeDrawingProgram->attachVertexShader(OpenglVertexShader::CreateFromFile("glshaders/plane.vert"));
		planeDrawingProgram->attachFragmentShader(OpenglFragmentShader::CreateFromFile("glshaders/plane.frag"));
		planeDrawingProgram->compile();

		planeDrawingProgram_uMVPMatrix = particlesDrawingProgram->registerUniform("uMVP");
	}

	void update(const size_t numParticles)
	{
		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

		glm::mat4 cameraVpMatrix = camera.vpMatrix();

		// render particles
		{
			glUseProgram(particlesDrawingProgram->mHandle);
			glEnableVertexAttribArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, particleMesh->mGl.mVerticesBuffer->mHandle);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
			particlesDrawingProgram_uMVPMatrix->setMat4(cameraVpMatrix);
			particlesDrawingProgram_uRadius->setFloat(radius);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, particlesDrawingProgram_ssboBinding, ssboBuffer);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, particleMesh->mGl.mIndicesBuffer->mHandle);
			glDrawElementsInstanced(GL_TRIANGLES, particleMesh->mNumTriangles * 3, GL_UNSIGNED_INT, (void*)0, numParticles);
			glDisableVertexAttribArray(0);
		}

		// render plane
		{
			glUseProgram(planeDrawingProgram->mHandle);
			glEnableVertexAttribArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, planeMesh->mGl.mVerticesBuffer->mHandle);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
			planeDrawingProgram_uMVPMatrix->setMat4(cameraVpMatrix);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, planeMesh->mGl.mIndicesBuffer->mHandle);
			glDrawElements(GL_TRIANGLES, planeMesh->mNumTriangles * 3, GL_UNSIGNED_INT, (void*)0);
			glDisableVertexAttribArray(0);
		}
	}

	float4* mapSsbo()
	{
		float4 *dptr;
		checkCudaErrors(cudaGraphicsMapResources(1, &ssboGraphicsRes, 0));
		size_t numBytes;
		checkCudaErrors(cudaGraphicsResourceGetMappedPointer((void **)&dptr, &numBytes, ssboGraphicsRes));
		return dptr;
	}

	void unmapSsbo()
	{
		checkCudaErrors(cudaGraphicsUnmapResources(1, &ssboGraphicsRes, 0));
	}

	float radius;
	std::shared_ptr<Mesh> particleMesh;
	std::shared_ptr<Mesh> planeMesh;
	Camera camera;
	GLuint ssboBuffer;
	cudaGraphicsResource_t ssboGraphicsRes;

private:
	GLuint globalVaoHandle;
};