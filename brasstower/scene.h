#pragma once

#include <iostream>
#include <memory>
#include <vector>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

#include "cuda/helper.cuh"
#include "mesh.h"

struct Plane
{
	glm::vec3 origin;
	glm::vec3 normal;
	glm::mat4 modelMatrix;

	Plane(const glm::vec3 & origin, const glm::vec3 & normal) :
		origin(origin), normal(normal)
	{
		// compute orthonormal bases for constructing rotation part of the matrix
		glm::vec3 zBasis = normal;
		glm::vec3 xBasis, yBasis;

		float sign = std::copysign((float)(1.0), zBasis.z);
		const float a = -1.0f / (sign + zBasis.z);
		const float b = zBasis.x * zBasis.y * a;
		xBasis = glm::vec3(1.0f + sign * zBasis.x * zBasis.x * a, sign * b, -sign * zBasis.x);
		yBasis = glm::vec3(b, sign + zBasis.y * zBasis.y * a, -zBasis.y);

		modelMatrix = glm::mat4(yBasis.x, yBasis.y, yBasis.z, 0.0f,
								zBasis.x, zBasis.y, zBasis.z, 0.0f,
								xBasis.x, xBasis.y, xBasis.z, 0.0f,
								origin.x, origin.y, origin.z, 1.f);
	}
};

struct RigidBody
{
	std::shared_ptr<Mesh> mesh;
	size_t numParticles;

	std::vector<glm::vec3> positions;
	std::vector<glm::vec3> positions_CM_Origin; // recomputed positions where all CM are placed at origin
	glm::vec3 CM; // center of mass
	glm::vec3 color;

	float massPerParticle;

	static std::shared_ptr<RigidBody> CreateRigidBox(const glm::vec3 & color,
													 const glm::ivec3 & dimension,
													 const glm::vec3 & startPosition,
													 const glm::vec3 & stepSize,
													 const float massPerParticle)
	{
		std::shared_ptr<RigidBody> result = std::make_shared<RigidBody>();
		result->color = color;
		result->massPerParticle = massPerParticle;

		std::vector<glm::vec3> & positions = result->positions;
		glm::vec3 CM = glm::vec3(0.0f);
		for (int i = 0; i < dimension.x; i++)
			for (int j = 0; j < dimension.y; j++)
				for (int k = 0; k < dimension.z; k++)
				{
					glm::vec3 position = startPosition + stepSize * glm::vec3(i, j, k);
					CM += position;
					positions.push_back(position);
				}

		CM /= static_cast<float>(positions.size());

		std::vector<glm::vec3> & positions_CM_Origin = result->positions_CM_Origin;
		for (int i = 0; i < positions.size(); i++)
			positions_CM_Origin.push_back(positions[i] - CM);

		result->CM = CM;

		result->mesh = MeshGenerator::Cube();
		glm::vec3 size = stepSize * glm::vec3(dimension) * 0.5f;
		result->mesh->applyTransform(glm::scale(size));
		result->mesh->createOpenglBuffer();
		return result;
	}

	RigidBody()
	{
	}
};

struct Granulars
{
	std::vector<glm::vec3> positions;
	float massPerParticle;

	static std::shared_ptr<Granulars> CreateGranularsBlock(const glm::ivec3 & dimension,
														   const glm::vec3 & startPosition,
														   const glm::vec3 & stepSize,
														   const float massPerParticle)
	{
		std::shared_ptr<Granulars> result = std::make_shared<Granulars>();
		result->massPerParticle = massPerParticle;

		std::vector<glm::vec3> & positions = result->positions;
		for (int i = 0; i < dimension.x; i++)
			for (int j = 0; j < dimension.y; j++)
				for (int k = 0; k < dimension.z; k++)
				{
					glm::vec3 position = startPosition + stepSize * glm::vec3(i, j, k);
					positions.push_back(position);
				}

		return result;
	}

	Granulars()
	{
	}
};

struct Fluid
{
	std::vector<glm::vec3> positions;
	float massPerParticle;

	static std::shared_ptr<Fluid> CreateFluidBlock(const glm::ivec3 & dimension,
												   const glm::vec3 & startPosition,
												   const glm::vec3 & stepSize,
												   const float massPerParticle)
	{
		std::shared_ptr<Fluid> result = std::make_shared<Fluid>();
		result->massPerParticle = massPerParticle;

		std::vector<glm::vec3> & positions = result->positions;
		for (int i = 0; i < dimension.x; i++)
			for (int j = 0; j < dimension.y; j++)
				for (int k = 0; k < dimension.z; k++)
				{
					glm::vec3 position = startPosition + stepSize * glm::vec3(i, j, k);
					positions.push_back(position);
				}

		return result;
	}
};

struct Rope
{
	std::vector<glm::vec3> positions;
	std::vector<glm::int2> distancePairs;
	std::vector<glm::vec2> distanceParams; // (distance, stiffness)
	float massPerParticle;

	static std::shared_ptr<Rope> CreateRope(const glm::vec3 & startPosition,
											const glm::vec3 & endPosition,
											const int numJoint,
											const float massPerParticle)
	{
		std::shared_ptr<Rope> result = std::make_shared<Rope>();
		result->massPerParticle = massPerParticle;

		std::vector<glm::vec3> & positions = result->positions;
		std::vector<glm::int2> & distancePairs = result->distancePairs;
		std::vector<glm::vec2> & distanceParams = result->distanceParams;
		glm::vec3 diff = endPosition - startPosition;
		float distance = glm::length(diff) / float(numJoint - 1);
		for (int i = 0; i < numJoint; i++)
		{
			positions.push_back(startPosition + float(i) / float(numJoint - 1) * diff);
		}
		for (int i = 0; i < numJoint - 1; i++)
		{
			distancePairs.push_back(glm::int2(i, i + 1));
			distanceParams.push_back(glm::vec2(distance, 1.0f));
		}
		for (int i = 1; i < numJoint - 1; i++)
		{
			distancePairs.push_back(glm::int2(i - 1, i + 1));
			distanceParams.push_back(glm::vec2(distance * 2.0f, 0.1f));
		}
		return result;
	}
};

struct Cloth
{
    std::vector<glm::vec3> positions;
    std::vector<glm::int2> distancePairs;
    std::vector<glm::vec2> distanceParams;
    std::vector<glm::int4> bendings;
	std::vector<glm::int3> faces;
	std::vector<int> immovables;
    float massPerParticle;

    static std::shared_ptr<Cloth> CreateCloth(const glm::vec3 & startPosition,
                                              const glm::vec3 & offsetX,
                                              const glm::vec3 & offsetY,
                                              const int numJointX,
                                              const int numJointY,
                                              const float massPerParticle,
											  const bool firstCorner = false,
											  const bool secondCorner = false,
											  const bool thirdCorner = false,
											  const bool fourthCorner = false)
    {
		const float stiffness = 0.2f;
		const float stiffness2 = 0.1f;
        std::shared_ptr<Cloth> result = std::make_shared<Cloth>();
        result->massPerParticle = massPerParticle;

        std::vector<glm::vec3> & positions = result->positions;
        std::vector<glm::int2> & distancePairs = result->distancePairs;
        std::vector<glm::vec2> & distanceParams = result->distanceParams;
        std::vector<glm::int4> & bendings = result->bendings;
		std::vector<glm::int3> & faces = result->faces;
		std::vector<int> & immovables = result->immovables;

        float lengthX = length(offsetX);
        float lengthY = length(offsetY);
        float lengthDiag = length(offsetX + offsetY);

		if (firstCorner) { immovables.push_back(0); }
		if (secondCorner) { immovables.push_back(numJointX - 1); }
		if (thirdCorner) { immovables.push_back(numJointX * (numJointY - 1)); }
		if (fourthCorner) { immovables.push_back(numJointX * numJointY - 1); }

        // init positions
        for (int x = 0; x < numJointX; x++)
        {
            for (int y = 0; y < numJointY; y++)
            {
				// positions
                positions.push_back(startPosition + y * offsetX + x * offsetY);

				int p1 = y * numJointX + x;
				int p2 = y * numJointX + x + 1;
				int p3 = (y + 1) * numJointX + x;
				int p4 = (y + 1) * numJointX + x + 1;

				// face
				{
					if (x < numJointX - 1 && y < numJointY - 1)
					{
						faces.push_back(glm::int3(p1, p2, p3));
						faces.push_back(glm::int3(p2, p3, p4));
					}
				}

                // distance pairs
				{
					// horizontal
					if (x < numJointX - 1)
					{
						distancePairs.push_back(glm::int2(p1, p2));
						distanceParams.push_back(glm::vec2(lengthX, stiffness));
					}

					// vertical
					if (y < numJointY - 1)
					{
						distancePairs.push_back(glm::int2(p1, p3));
						distanceParams.push_back(glm::vec2(lengthY, stiffness));
					}

					// diagonal1
					if (x < numJointX - 1 && y < numJointY - 1)
					{
						distancePairs.push_back(glm::int2(p1, p4));
						distanceParams.push_back(glm::vec2(lengthDiag, stiffness));

						distancePairs.push_back(glm::int2(p2, p3));
						distanceParams.push_back(glm::vec2(lengthDiag, stiffness));
					}
				}


				// hack bending
				{
					int q[3][3];
					for (int i = -1; i <= 1;i++)
					{
						for (int j = -1; j <= 1;j++)
						{
							q[i + 1][j + 1] = (y + i) * numJointX + x + j;
						}
					}

					if (x > 0 && y > 0 && x < numJointX - 1 && y < numJointY - 1)
					{
						for (int i = 0; i < 3; i++)
						{
							distancePairs.push_back(glm::int2(q[i][0], q[i][2]));
							distanceParams.push_back(glm::vec2(lengthX * 2, stiffness2));
						}

						for (int i = 0; i < 3; i++)
						{
							distancePairs.push_back(glm::int2(q[0][i], q[2][i]));
							distanceParams.push_back(glm::vec2(lengthY * 2, stiffness2));
						}

					}
				}

				// bending constraints
				/*
				{
					if (x < numJointX - 1 && y < numJointY - 1)
					{
						if ((x + y) % 2)
						{
							bendings.push_back(glm::int4(p3, p2, p1, p4));
						}
						else
						{
							bendings.push_back(glm::int4(p1, p4, p3, p2));
						}
					}
				}
				*/
            }
        }
        return result;
    }
};

struct Camera
{
	Camera() {}

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
		pos += mBasisZ * move.z + mBasisX * move.x + up * move.y;
	}

	void rotate(const glm::vec2 & rotation)
	{
		thetaPhi += rotation;
		dir = SphericalToWorld(thetaPhi);
	}

	glm::mat4 vpMatrix()
	{
		glm::mat4 viewMatrix = glm::lookAt(pos, dir + pos, glm::vec3(0, 1, 0));
		glm::mat4 projMatrix = glm::perspective(fovY, aspectRatio, 0.05f, 100.0f);
		return projMatrix * viewMatrix;
	}

	float fovY;
	float aspectRatio;
	glm::vec3 pos;
	glm::vec2 thetaPhi;
	glm::vec3 dir;
	glm::vec3 up;
};

struct PointLight
{
	glm::mat4 shadowMatrix()
	{
		glm::mat4 projMatrix = glm::perspective(thetaMinMax.y * 2.0f, 1.0f, 0.5f, 100.0f);
		glm::mat4 viewMatrix = glm::lookAt(position, position + direction, glm::vec3(0, 1, 0));
		return projMatrix * viewMatrix;
	}

	glm::vec3 position;
	glm::vec3 direction;
	glm::vec3 intensity;
	glm::vec2 thetaMinMax = glm::radians(glm::vec2(45.f, 50.0f));
};

struct OldSceneFormat
{
	OldSceneFormat()
	{}

	std::vector<Plane> planes;
	Camera camera;
	std::vector<std::shared_ptr<RigidBody>> rigidBodies;
	std::vector<std::shared_ptr<Granulars>> granulars; // position of solid particles (without any constraints)
	std::vector<std::shared_ptr<Fluid>> fluids;
	std::vector<std::shared_ptr<Rope>> ropes;
    std::vector<std::shared_ptr<Cloth>> clothes;

	PointLight pointLight;

	float fluidRestDensity;

	/// THESE ARE PARTICLE SYSTEM PARAMETERS! don't touch!
	/// TODO:: move these to particle system
	size_t numParticles = 0;
	size_t numMaxParticles = 0;
	size_t numRigidBodies = 0;
	size_t numMaxRigidBodies = 0;
	size_t numDistancePairs = 0;
	size_t numMaxDistancePairs = 0;
	size_t numBendings = 0;
	size_t numMaxBendings = 0;
	size_t numWindFaces = 0;
	size_t numMaxWindFaces = 0;
	size_t numImmovables = 0;
	size_t numMaxImmovables = 0;
	float radius;
};

struct alignas(16) DistanceConstraint
{
	int2					ids;
	float					distance;
	float					kStiff;

	DistanceConstraint(const int id1,
					   const int id2,
					   const float distance,
					   const float kStiff) :
		ids(make_int2(id1, id2)),
		distance(distance),
		kStiff(kStiff)
	{}
};

// prepare data in CPU side first then put it in GPU later
// data that can't be stored inside CPU can't be fit in GPU anyway. 
struct Scene
{
	Scene() {}

	// all add methods return range of start positions
	int2 addCloth(const float3 & startPosition,
				  const float3 & stepX,
				  const float3 & stepY,
				  const int numJointX,
				  const int numJointY,
				  const float massPerParticle,
				  const float kStiffness,
				  const float kBending,
				  const bool isSelfCollidable = true);

	int2 addFluidBlock(const uint3 & dimension,
					   const float3 & startPosition,
					   const glm::vec3 & stepX,
					   const glm::vec3 & stepY,
					   const float massPerParticle);

	int2 addGranularsBlock(const uint3 & dimension,
						   const float3 & startPosition,
						   const float3 & step,
						   const float massPerParticle);

	int2 addRigidBox(const glm::ivec3 & dimension,
					 const glm::vec3 & startPosition,
					 const glm::vec3 & stepX,
					 const glm::vec3 & stepY,
					 const float massPerParticle);

	int2 addRope(const float3 & startPosition,
				 const float3 & step,
				 const int numJoint,
				 const float massPerParticle);

	/// TODO::int2 addRigidBody

	/// TODO::int2 addSoftBody

	void makeImmovable(const int id);

	size_t numParticles() { return positions.size(); }

	float								particleRadius = 0.05f;
	float								fluidKernelRadius = 2.3 * particleRadius;

	Camera								camera;
	PointLight							pointLight;

	// particle data
	std::vector<float3>					positions;
	std::vector<float>					masses;
	std::vector<int>					phases;
	std::vector<int>					groupIds;

	// colliding objects
	std::vector<Plane>					planes;

	// aerodynamic faces - record all 3 ids of each triangle face
	std::vector<int3>					faces;

	// constraints

	// rigidbody constraints - record all initial positions - expected center of mass at 0
	std::vector<float3>					rigidbodyInitialPositions;
	std::vector<int2>					rigidbodyParticleIdRanges;
	// distance constraints
	/// TODO:: pack distance pairs and distance params together
	std::vector<int2>					distancePairs;
	std::vector<float2>					distanceParams;
	// bending constraints - record all 4 ids
	std::vector<int4>					bendingConstraints;
	// immovable constraints - record ids that should be immovable
	std::vector<int>					immovableConstraints;

	// phase counter for phases
	int									groupIdCounter = 1;
	int									solidPhaseCounter = 1;
	int									fluidPhaseCounter = -1;
};