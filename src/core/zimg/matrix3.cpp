#include <cstddef>
#include "matrix3.h"

namespace zimg::colorspace {

namespace {

double det2(double a00, double a01, double a10, double a11)
{
	return a00 * a11 - a01 * a10;
}

} // namespace


Vector3 operator*(const Vector3 &v1, const Vector3 &v2) noexcept
{
	Vector3 ret;

	for (size_t i = 0; i < 3; ++i) {
		ret[i] = v1[i] * v2[i];
	}
	return ret;
}

Vector3 operator*(const Matrix3x3 &m, const Vector3 &v) noexcept
{
	Vector3 ret;

	for (size_t i = 0; i < 3; ++i) {
		double accum = 0;

		for (size_t k = 0; k < 3; ++k) {
			accum += m[i][k] * v[k];
		}
		ret[i] = accum;
	}
	return ret;
}

Matrix3x3 operator*(const Matrix3x3 &a, const Matrix3x3 &b) noexcept
{
	Matrix3x3 ret;

	for (size_t i = 0; i < 3; ++i) {
		for (size_t j = 0; j < 3; ++j) {
			double accum = 0;

			for (size_t k = 0; k < 3; ++k) {
				accum += a[i][k] * b[k][j];
			}
			ret[i][j] = accum;
		}
	}
	return ret;
}

Vector3 cross(const Vector3 &a, const Vector3 &b) noexcept
{
	return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

double dot(const Vector3 &a, const Vector3 &b) noexcept
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

double determinant(const Matrix3x3 &m) noexcept
{
	double det = 0;

	det += m[0][0] * det2(m[1][1], m[1][2], m[2][1], m[2][2]);
	det -= m[0][1] * det2(m[1][0], m[1][2], m[2][0], m[2][2]);
	det += m[0][2] * det2(m[1][0], m[1][1], m[2][0], m[2][1]);

	return det;
}

Matrix3x3 inverse(const Matrix3x3 &m) noexcept
{
	Matrix3x3 ret;
	double det = determinant(m);

	ret[0][0] = det2(m[1][1], m[1][2], m[2][1], m[2][2]) / det;
	ret[0][1] = det2(m[0][2], m[0][1], m[2][2], m[2][1]) / det;
	ret[0][2] = det2(m[0][1], m[0][2], m[1][1], m[1][2]) / det;
	ret[1][0] = det2(m[1][2], m[1][0], m[2][2], m[2][0]) / det;
	ret[1][1] = det2(m[0][0], m[0][2], m[2][0], m[2][2]) / det;
	ret[1][2] = det2(m[0][2], m[0][0], m[1][2], m[1][0]) / det;
	ret[2][0] = det2(m[1][0], m[1][1], m[2][0], m[2][1]) / det;
	ret[2][1] = det2(m[0][1], m[0][0], m[2][1], m[2][0]) / det;
	ret[2][2] = det2(m[0][0], m[0][1], m[1][0], m[1][1]) / det;

	return ret;
}

Matrix3x3 transpose(const Matrix3x3 &m) noexcept
{
	Matrix3x3 ret;

	for (size_t i = 0; i < 3; ++i) {
		for (size_t j = 0; j < 3; ++j) {
			ret[i][j] = m[j][i];
		}
	}
	return ret;
}

} // namespace zimg::colorspace
