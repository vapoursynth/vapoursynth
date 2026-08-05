#pragma once

#ifndef ZIMG_COLORSPACE_MATRIX3_H_
#define ZIMG_COLORSPACE_MATRIX3_H_

#include <array>

namespace zimg::colorspace {

/**
 * Fixed size vector of 3 numbers.
 */
struct Vector3 : public std::array<double, 3> {
	Vector3() = default;

	constexpr Vector3(double a, double b, double c) :
		std::array<double, 3>{ { a, b, c } }
	{}
};

/**
 * Fixed size 3x3 matrix.
 */
struct Matrix3x3 : public std::array<Vector3, 3> {
	Matrix3x3() = default;

	constexpr Matrix3x3(const Vector3 &a, const Vector3 &b, const Vector3 &c) :
		std::array<Vector3, 3>{ { a, b, c } }
	{}

	static constexpr Matrix3x3 identity()
	{
		return{
			{ 1.0, 0.0, 0.0 },
			{ 0.0, 1.0, 0.0 },
			{ 0.0, 0.0, 1.0 }
		};
	}
};

/**
 * Element-wise multiplication between vectors.
 *
 * @param v1 lhs
 * @param v2 rhs
 * @return element-wise product
 */
Vector3 operator*(const Vector3 &v1, const Vector3 &v2) noexcept;

/**
 * Matrix-vector multiplication.
 *
 * @param m matrix
 * @param v vector
 * @return product
 */
Vector3 operator*(const Matrix3x3 &m, const Vector3 &v) noexcept;

/**
 * Matrix-matrix multiplication.
 *
 * @param a lhs
 * @param b rhs
 * @return product
 */
Matrix3x3 operator*(const Matrix3x3 &a, const Matrix3x3 &b) noexcept;

/**
 * Vector cross product.
  *
 * @param a lhs
 * @param b rhs
 * @return product
 */
Vector3 cross(const Vector3 &a, const Vector3 &b) noexcept;

/**
 * Vector dot product.
 *
 * @param a lhs
 * @param b rhs
 * @return product
 */
double dot(const Vector3 &a, const Vector3 &b) noexcept;

/**
 * Determinant of matrix.
 *
 * @param m matrix
 * @return determinant
 */
double determinant(const Matrix3x3 &m) noexcept;

/**
 * Inverse of matrix.
 *
 * @param m matrix
 * @return inverse
 */
Matrix3x3 inverse(const Matrix3x3 &m) noexcept;

/**
 * Transpose of matrix.
 *
 * @param m matrix
 * @return transpose
 */
Matrix3x3 transpose(const Matrix3x3 &m) noexcept;

} // namespace zimg::colorspace

#endif // ZIMG_COLORSPACE_MATRIX3_H_
