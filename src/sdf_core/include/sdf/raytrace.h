#pragma once

#include <cstdint>
#include <vector>

#include "sdf/mesh.h"
#include "sdf/types.h"

namespace sdf
{

struct Ray
{
  Vec3 origin;
  Vec3 direction;
};

struct RayHit
{
  float t = 1.0e30f;
  int triangle_index = -1;
  float barycentric_u = 0.0f;
  float barycentric_v = 0.0f;
  float barycentric_w = 0.0f;
  Vec3 position;
  Vec3 geometric_normal;
  Vec3 shading_normal;
};

struct RayTriangle
{
  Vec3 v0;
  Vec3 v1;
  Vec3 v2;
  Vec3 n0;
  Vec3 n1;
  Vec3 n2;
  Vec3 face_normal;
  Vec3 centroid;
  Aabb bounds;
  std::uint32_t material_id = 0;
  std::uint32_t source_triangle_index = 0;
};

struct RayBvhNode
{
  Aabb bounds;
  int left = -1;
  int right = -1;
  int first_triangle = 0;
  int triangle_count = 0;

  bool is_leaf() const
  {
    return left < 0 && right < 0;
  }
};

struct RayScene
{
  std::vector<RayTriangle> triangles;
  std::vector<RayBvhNode> bvh_nodes;
  Aabb bounds = make_empty_aabb();
};

RayScene build_ray_scene(const Mesh &mesh);
bool intersect_ray(const RayScene &scene, const Ray &ray, float max_distance, RayHit *hit);
bool is_occluded(const RayScene &scene, const Ray &ray, float max_distance);

}  // namespace sdf

