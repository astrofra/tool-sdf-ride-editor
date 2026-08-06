#include "sdf/raytrace.h"

#include <algorithm>
#include <cmath>

namespace sdf
{

namespace
{

constexpr float kHuge = 1.0e30f;
constexpr float kEpsilon = 1.0e-4f;
constexpr int kLeafTriangleThreshold = 4;

float component_by_axis(const Vec3 &value, int axis)
{
  return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
}

Aabb compute_triangle_range_bounds(const std::vector<RayTriangle> &triangles, int start, int end)
{
  Aabb bounds = make_empty_aabb();
  for (int index = start; index < end; ++index)
  {
    expand_aabb(bounds, triangles[index].bounds);
  }
  return bounds;
}

Aabb compute_triangle_range_centroid_bounds(const std::vector<RayTriangle> &triangles, int start, int end)
{
  Aabb bounds = make_empty_aabb();
  for (int index = start; index < end; ++index)
  {
    expand_aabb(bounds, triangles[index].centroid);
  }
  return bounds;
}

int longest_axis(const Aabb &bounds)
{
  const Vec3 extent = bounds.max - bounds.min;
  if (extent.y > extent.x && extent.y >= extent.z)
  {
    return 1;
  }
  return extent.z > extent.x ? 2 : 0;
}

int build_bvh_recursive(RayScene *scene, int start, int end)
{
  const int node_index = static_cast<int>(scene->bvh_nodes.size());
  scene->bvh_nodes.push_back(RayBvhNode{});

  RayBvhNode node;
  node.bounds = compute_triangle_range_bounds(scene->triangles, start, end);

  const int triangle_count = end - start;
  if (triangle_count <= kLeafTriangleThreshold)
  {
    node.first_triangle = start;
    node.triangle_count = triangle_count;
    scene->bvh_nodes[node_index] = node;
    return node_index;
  }

  const Aabb centroid_bounds = compute_triangle_range_centroid_bounds(scene->triangles, start, end);
  const int split_axis = longest_axis(centroid_bounds);
  const int middle = start + triangle_count / 2;

  std::nth_element(
    scene->triangles.begin() + start,
    scene->triangles.begin() + middle,
    scene->triangles.begin() + end,
    [&](const RayTriangle &lhs, const RayTriangle &rhs)
    {
      return component_by_axis(lhs.centroid, split_axis) < component_by_axis(rhs.centroid, split_axis);
    });

  node.left = build_bvh_recursive(scene, start, middle);
  node.right = build_bvh_recursive(scene, middle, end);

  scene->bvh_nodes[node_index] = node;
  return node_index;
}

bool intersect_aabb(const Ray &ray, const Aabb &bounds, float max_distance)
{
  float t_min = kEpsilon;
  float t_max = max_distance;

  for (int axis = 0; axis < 3; ++axis)
  {
    const float origin = component_by_axis(ray.origin, axis);
    const float direction = component_by_axis(ray.direction, axis);
    const float min_bound = component_by_axis(bounds.min, axis);
    const float max_bound = component_by_axis(bounds.max, axis);

    if (std::fabs(direction) <= kEpsilon)
    {
      if (origin < min_bound || origin > max_bound)
      {
        return false;
      }
      continue;
    }

    const float inverse_direction = 1.0f / direction;
    float t0 = (min_bound - origin) * inverse_direction;
    float t1 = (max_bound - origin) * inverse_direction;
    if (t0 > t1)
    {
      std::swap(t0, t1);
    }

    t_min = std::max(t_min, t0);
    t_max = std::min(t_max, t1);
    if (t_max < t_min)
    {
      return false;
    }
  }

  return true;
}

bool intersect_triangle(const Ray &ray, const RayTriangle &triangle, float max_distance, RayHit *hit)
{
  const Vec3 edge1 = triangle.v1 - triangle.v0;
  const Vec3 edge2 = triangle.v2 - triangle.v0;
  const Vec3 pvec = cross(ray.direction, edge2);
  const float determinant = dot(edge1, pvec);

  if (determinant > -kEpsilon && determinant < kEpsilon)
  {
    return false;
  }

  const float inverse_determinant = 1.0f / determinant;
  const Vec3 tvec = ray.origin - triangle.v0;
  const float u = dot(tvec, pvec) * inverse_determinant;
  if (u < 0.0f || u > 1.0f)
  {
    return false;
  }

  const Vec3 qvec = cross(tvec, edge1);
  const float v = dot(ray.direction, qvec) * inverse_determinant;
  if (v < 0.0f || u + v > 1.0f)
  {
    return false;
  }

  const float t = dot(edge2, qvec) * inverse_determinant;
  if (t <= kEpsilon || t >= max_distance || t >= hit->t)
  {
    return false;
  }

  hit->t = t;
  hit->triangle_index = static_cast<int>(triangle.source_triangle_index);
  hit->barycentric_u = u;
  hit->barycentric_v = v;
  hit->barycentric_w = 1.0f - u - v;
  hit->position = ray.origin + ray.direction * t;
  hit->geometric_normal = triangle.face_normal;
  hit->shading_normal = normalized(
    triangle.n0 * hit->barycentric_w +
    triangle.n1 * hit->barycentric_u +
    triangle.n2 * hit->barycentric_v);

  if (length_squared(hit->shading_normal) <= kEpsilon)
  {
    hit->shading_normal = triangle.face_normal;
  }

  if (dot(hit->geometric_normal, ray.direction) > 0.0f)
  {
    hit->geometric_normal = -hit->geometric_normal;
  }
  if (dot(hit->shading_normal, ray.direction) > 0.0f)
  {
    hit->shading_normal = -hit->shading_normal;
  }

  return true;
}

}  // namespace

RayScene build_ray_scene(const Mesh &mesh)
{
  RayScene scene;
  scene.bounds = make_empty_aabb();
  scene.triangles.reserve(mesh.triangles.size());

  for (std::size_t triangle_index = 0; triangle_index < mesh.triangles.size(); ++triangle_index)
  {
    const MeshTriangle &source = mesh.triangles[triangle_index];
    if (source.i0 >= mesh.vertices.size() || source.i1 >= mesh.vertices.size() || source.i2 >= mesh.vertices.size())
    {
      continue;
    }

    const MeshVertex &v0 = mesh.vertices[source.i0];
    const MeshVertex &v1 = mesh.vertices[source.i1];
    const MeshVertex &v2 = mesh.vertices[source.i2];

    RayTriangle triangle;
    triangle.v0 = v0.position;
    triangle.v1 = v1.position;
    triangle.v2 = v2.position;
    triangle.n0 = normalized(v0.normal);
    triangle.n1 = normalized(v1.normal);
    triangle.n2 = normalized(v2.normal);
    triangle.face_normal = normalized(cross(triangle.v1 - triangle.v0, triangle.v2 - triangle.v0));
    triangle.centroid = (triangle.v0 + triangle.v1 + triangle.v2) / 3.0f;
    triangle.bounds = make_empty_aabb();
    expand_aabb(triangle.bounds, triangle.v0);
    expand_aabb(triangle.bounds, triangle.v1);
    expand_aabb(triangle.bounds, triangle.v2);
    triangle.material_id = source.material_id;
    triangle.source_triangle_index = static_cast<std::uint32_t>(triangle_index);

    if (length_squared(triangle.face_normal) <= kEpsilon)
    {
      continue;
    }

    if (length_squared(triangle.n0) <= kEpsilon)
    {
      triangle.n0 = triangle.face_normal;
    }
    if (length_squared(triangle.n1) <= kEpsilon)
    {
      triangle.n1 = triangle.face_normal;
    }
    if (length_squared(triangle.n2) <= kEpsilon)
    {
      triangle.n2 = triangle.face_normal;
    }

    scene.triangles.push_back(triangle);
    expand_aabb(scene.bounds, triangle.bounds);
  }

  if (!scene.triangles.empty())
  {
    build_bvh_recursive(&scene, 0, static_cast<int>(scene.triangles.size()));
  }

  return scene;
}

bool intersect_ray(const RayScene &scene, const Ray &ray, float max_distance, RayHit *hit)
{
  if (hit == nullptr || scene.bvh_nodes.empty())
  {
    return false;
  }

  hit->t = max_distance;
  hit->triangle_index = -1;

  int stack[128];
  int stack_size = 0;
  stack[stack_size++] = 0;

  bool found_hit = false;
  while (stack_size > 0)
  {
    const int node_index = stack[--stack_size];
    const RayBvhNode &node = scene.bvh_nodes[node_index];

    if (!intersect_aabb(ray, node.bounds, hit->t))
    {
      continue;
    }

    if (node.is_leaf())
    {
      for (int index = 0; index < node.triangle_count; ++index)
      {
        if (intersect_triangle(ray, scene.triangles[node.first_triangle + index], max_distance, hit))
        {
          found_hit = true;
        }
      }
      continue;
    }

    if (node.left >= 0)
    {
      stack[stack_size++] = node.left;
    }
    if (node.right >= 0)
    {
      stack[stack_size++] = node.right;
    }
  }

  return found_hit;
}

bool is_occluded(const RayScene &scene, const Ray &ray, float max_distance)
{
  RayHit hit;
  return intersect_ray(scene, ray, max_distance, &hit);
}

}  // namespace sdf

