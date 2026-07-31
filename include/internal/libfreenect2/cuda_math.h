/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2026 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses.
 */

#ifndef LIBFREENECT2_CUDA_MATH_H_
#define LIBFREENECT2_CUDA_MATH_H_

#include <vector_functions.h>
#include <vector_types.h>

static inline __host__ __device__ float3 make_float3(float value)
{
  return make_float3(value, value, value);
}

static inline __host__ __device__ float3 make_float3(float4 value)
{
  return make_float3(value.x, value.y, value.z);
}

static inline __host__ __device__ float4 make_float4(float3 value)
{
  return make_float4(value.x, value.y, value.z, 0.0f);
}

static inline __host__ __device__ int3 make_int3(int value)
{
  return make_int3(value, value, value);
}

static inline __host__ __device__ float2 operator*(float2 vector, float scalar)
{
  return make_float2(vector.x * scalar, vector.y * scalar);
}

static inline __host__ __device__ float3 operator-(float3 vector)
{
  return make_float3(-vector.x, -vector.y, -vector.z);
}

static inline __host__ __device__ float3 operator+(float3 left, float3 right)
{
  return make_float3(left.x + right.x, left.y + right.y, left.z + right.z);
}

static inline __host__ __device__ float3 operator+(float3 vector, float scalar)
{
  return make_float3(vector.x + scalar, vector.y + scalar, vector.z + scalar);
}

static inline __host__ __device__ float3 operator+(float scalar, float3 vector)
{
  return vector + scalar;
}

static inline __host__ __device__ float3 operator-(float3 left, float3 right)
{
  return make_float3(left.x - right.x, left.y - right.y, left.z - right.z);
}

static inline __host__ __device__ float3 operator-(float3 vector, float scalar)
{
  return make_float3(vector.x - scalar, vector.y - scalar, vector.z - scalar);
}

static inline __host__ __device__ float3 operator-(float scalar, float3 vector)
{
  return make_float3(scalar - vector.x, scalar - vector.y, scalar - vector.z);
}

static inline __host__ __device__ float3 operator*(float3 left, float3 right)
{
  return make_float3(left.x * right.x, left.y * right.y, left.z * right.z);
}

static inline __host__ __device__ float3 operator*(float3 vector, float scalar)
{
  return make_float3(vector.x * scalar, vector.y * scalar, vector.z * scalar);
}

static inline __host__ __device__ float3 operator*(float scalar, float3 vector)
{
  return vector * scalar;
}

static inline __host__ __device__ float3 operator/(float3 left, float3 right)
{
  return make_float3(left.x / right.x, left.y / right.y, left.z / right.z);
}

static inline __host__ __device__ float3 operator/(float3 vector, float scalar)
{
  return make_float3(vector.x / scalar, vector.y / scalar, vector.z / scalar);
}

static inline __host__ __device__ float3 operator/(float scalar, float3 vector)
{
  return make_float3(scalar / vector.x, scalar / vector.y, scalar / vector.z);
}

static inline __host__ __device__ void operator+=(float3 &left, float3 right)
{
  left = left + right;
}

static inline __host__ __device__ void operator*=(float3 &left, float3 right)
{
  left = left * right;
}

static inline __host__ __device__ float dot(float3 left, float3 right)
{
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

static inline __host__ __device__ float clamp(float value, float minimum, float maximum)
{
  return value < minimum ? minimum : (value > maximum ? maximum : value);
}

#endif // LIBFREENECT2_CUDA_MATH_H_
