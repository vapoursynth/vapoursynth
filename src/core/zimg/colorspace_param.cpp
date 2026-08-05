#include <cstring>
#include "except.h"
#include "colorspace.h"
#include "colorspace_param.h"
#include "matrix3.h"

namespace zimg::colorspace {

namespace {

void get_yuv_constants(double *kr, double *kb, MatrixCoefficients matrix)
{
	switch (matrix) {
	case MatrixCoefficients::RGB:
		*kr = 0;
		*kb = 0;
		break;
	case MatrixCoefficients::FCC:
		*kr = FCC_KR;
		*kb = FCC_KB;
		break;
	case MatrixCoefficients::SMPTE_240M:
		*kr = SMPTE_240M_KR;
		*kb = SMPTE_240M_KB;
		break;
	case MatrixCoefficients::REC_601:
		*kr = REC_601_KR;
		*kb = REC_601_KB;
		break;
	case MatrixCoefficients::REC_709:
		*kr = REC_709_KR;
		*kb = REC_709_KB;
		break;
	case MatrixCoefficients::REC_2020_NCL:
	case MatrixCoefficients::REC_2020_CL:
		*kr = REC_2020_KR;
		*kb = REC_2020_KB;
		break;
	default:
		error::throw_<error::InternalError>("unrecognized matrix coefficients");
	}
}

Vector3 xy_to_xyz(double x, double y)
{
	Vector3 ret;

	ret[0] = x / y;
	ret[1] = 1.0;
	ret[2] = (1.0 - x - y) / y;

	return ret;
}

Vector3 get_white_point(ColorPrimaries primaries)
{
	switch (primaries) {
	case ColorPrimaries::REC_470_M:
	case ColorPrimaries::FILM:
		return xy_to_xyz(ILLUMINANT_C[0], ILLUMINANT_C[1]);
	case ColorPrimaries::XYZ:
		return xy_to_xyz(ILLUMINANT_E[0], ILLUMINANT_E[1]);
	case ColorPrimaries::DCI_P3:
		return xy_to_xyz(ILLUMINANT_DCI[0], ILLUMINANT_DCI[1]);
	default:
		return xy_to_xyz(ILLUMINANT_D65[0], ILLUMINANT_D65[1]);
	}
}

void get_primaries_xy(double out[3][2], ColorPrimaries primaries)
{
	switch (primaries) {
	case ColorPrimaries::REC_470_M:
		memcpy(out, REC_470_M_PRIMARIES, sizeof(REC_470_M_PRIMARIES));
		break;
	case ColorPrimaries::REC_470_BG:
		memcpy(out, REC_470_BG_PRIMARIES, sizeof(REC_470_BG_PRIMARIES));
		break;
	case ColorPrimaries::SMPTE_C:
		memcpy(out, SMPTE_C_PRIMARIES, sizeof(SMPTE_C_PRIMARIES));
		break;
	case ColorPrimaries::REC_709:
		memcpy(out, REC_709_PRIMARIES, sizeof(REC_709_PRIMARIES));
		break;
	case ColorPrimaries::FILM:
		memcpy(out, FILM_PRIMARIES, sizeof(FILM_PRIMARIES));
		break;
	case ColorPrimaries::REC_2020:
		memcpy(out, REC_2020_PRIMARIES, sizeof(REC_2020_PRIMARIES));
		break;
	case ColorPrimaries::DCI_P3:
	case ColorPrimaries::DCI_P3_D65:
		memcpy(out, DCI_P3_PRIMARIES, sizeof(DCI_P3_PRIMARIES));
		break;
	case ColorPrimaries::EBU_3213_E:
		memcpy(out, EBU_3213_E_PRIMARIES, sizeof(EBU_3213_E_PRIMARIES));
		break;
	default:
		error::throw_<error::InternalError>("unrecognized primaries");
	}
}

Matrix3x3 get_primaries_xyz(ColorPrimaries primaries)
{
	// Columns: R G B
	// Rows: X Y Z
	Matrix3x3 ret;
	double primaries_xy[3][2];

	get_primaries_xy(primaries_xy, primaries);

	ret[0] = xy_to_xyz(primaries_xy[0][0], primaries_xy[0][1]);
	ret[1] = xy_to_xyz(primaries_xy[1][0], primaries_xy[1][1]);
	ret[2] = xy_to_xyz(primaries_xy[2][0], primaries_xy[2][1]);

	return transpose(ret);
}

void get_yuv_constants_from_primaries(double *kr, double *kb, ColorPrimaries primaries)
{
	// ITU-T H.265 Annex E, Eq (E-22) to (E-27).
	double primaries_xy[3][2];
	get_primaries_xy(primaries_xy, primaries);

	Vector3 r_xyz = xy_to_xyz(primaries_xy[0][0], primaries_xy[0][1]);
	Vector3 g_xyz = xy_to_xyz(primaries_xy[1][0], primaries_xy[1][1]);
	Vector3 b_xyz = xy_to_xyz(primaries_xy[2][0], primaries_xy[2][1]);
	Vector3 white_xyz = get_white_point(primaries);

	Vector3 x_rgb = { r_xyz[0], g_xyz[0], b_xyz[0] };
	Vector3 y_rgb = { r_xyz[1], g_xyz[1], b_xyz[1] };
	Vector3 z_rgb = { r_xyz[2], g_xyz[2], b_xyz[2] };

	*kr = dot(white_xyz, cross(g_xyz, b_xyz)) / dot(x_rgb, cross(y_rgb, z_rgb));
	*kb = dot(white_xyz, cross(r_xyz, g_xyz)) / dot(x_rgb, cross(y_rgb, z_rgb));
}

Matrix3x3 ncl_rgb_to_yuv_matrix_from_kr_kb(double kr, double kb)
{
	Matrix3x3 ret;
	double kg = 1.0 - kr - kb;
	double uscale;
	double vscale;

	uscale = 1.0 / (2.0 - 2.0 * kb);
	vscale = 1.0 / (2.0 - 2.0 * kr);

	ret[0][0] = kr;
	ret[0][1] = kg;
	ret[0][2] = kb;

	ret[1][0] = -kr * uscale;
	ret[1][1] = -kg * uscale;
	ret[1][2] = (1.0 - kb) * uscale;

	ret[2][0] = (1.0 - kr) * vscale;
	ret[2][1] = -kg * vscale;
	ret[2][2] = -kb * vscale;

	return ret;
}

} // namespace


Matrix3x3 ncl_yuv_to_rgb_matrix(MatrixCoefficients matrix)
{
	return inverse(ncl_rgb_to_yuv_matrix(matrix));
}

Matrix3x3 ncl_rgb_to_yuv_matrix(MatrixCoefficients matrix)
{
	double kr, kb;

	switch (matrix)
	{
	case MatrixCoefficients::YCGCO:
		return {
			{  0.25, 0.5,  0.25 },
			{ -0.25, 0.5, -0.25 },
			{  0.5,  0,   -0.5 }
		};
	case MatrixCoefficients::REC_2100_LMS:
		return {
			{ 1688.0 / 4096.0, 2146.0 / 4096.0,  262.0 / 4096.0 },
			{  683.0 / 4096.0, 2951.0 / 4096.0,  462.0 / 4096.0 },
			{   99.0 / 4096.0,  309.0 / 4096.0, 3688.0 / 4096.0 }
		};
	default:
		get_yuv_constants(&kr, &kb, matrix);
		return ncl_rgb_to_yuv_matrix_from_kr_kb(kr, kb);
	}
}

Matrix3x3 ncl_yuv_to_rgb_matrix_from_primaries(ColorPrimaries primaries)
{
	double kr, kb;

	switch (primaries) {
	case ColorPrimaries::REC_709:
		return ncl_yuv_to_rgb_matrix(MatrixCoefficients::REC_709);
	case ColorPrimaries::REC_2020:
		return ncl_yuv_to_rgb_matrix(MatrixCoefficients::REC_2020_NCL);
	default:
		get_yuv_constants_from_primaries(&kr, &kb, primaries);
		return inverse(ncl_rgb_to_yuv_matrix_from_kr_kb(kr, kb));
	}
}

Matrix3x3 ncl_rgb_to_yuv_matrix_from_primaries(ColorPrimaries primaries)
{
	double kr, kb;

	switch (primaries) {
	case ColorPrimaries::REC_709:
		return ncl_rgb_to_yuv_matrix(MatrixCoefficients::REC_709);
	case ColorPrimaries::REC_2020:
		return ncl_rgb_to_yuv_matrix(MatrixCoefficients::REC_2020_NCL);
	default:
		get_yuv_constants_from_primaries(&kr, &kb, primaries);
		return ncl_rgb_to_yuv_matrix_from_kr_kb(kr, kb);
	}
}

Matrix3x3 ictcp_to_lms_matrix(TransferCharacteristics transfer)
{
	return inverse(lms_to_ictcp_matrix(transfer));
}

Matrix3x3 lms_to_ictcp_matrix(TransferCharacteristics transfer)
{
	if (transfer == TransferCharacteristics::ARIB_B67) {
		return{
			{              0.5,               0.5,             0.0 },
			{  3625.0 / 4096.0,  -7465.0 / 4096.0, 3840.0 / 4096.0 },
			{  9500.0 / 4096.0,  -9212.0 / 4096.0, -288.0 / 4096.0 }
		};
	} else {
		return {
			{              0.5,               0.5,             0.0 },
			{  6610.0 / 4096.0, -13613.0 / 4096.0, 7003.0 / 4096.0 },
			{ 17933.0 / 4096.0, -17390.0 / 4096.0, -543.0 / 4096.0 }
		};
	}
}

// http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
Matrix3x3 gamut_rgb_to_xyz_matrix(ColorPrimaries primaries)
{
	if (primaries == ColorPrimaries::XYZ)
		return Matrix3x3::identity();

	Matrix3x3 xyz_matrix = get_primaries_xyz(primaries);
	Vector3 white_xyz = get_white_point(primaries);

	Vector3 s = inverse(xyz_matrix) * white_xyz;
	Matrix3x3 m = { xyz_matrix[0] * s, xyz_matrix[1] * s, xyz_matrix[2] * s };

	return m;
}

Matrix3x3 gamut_xyz_to_rgb_matrix(ColorPrimaries primaries)
{
	if (primaries == ColorPrimaries::XYZ)
		return Matrix3x3::identity();

	return inverse(gamut_rgb_to_xyz_matrix(primaries));
}

// http://www.brucelindbloom.com/index.html?Eqn_ChromAdapt.html
Matrix3x3 white_point_adaptation_matrix(ColorPrimaries in, ColorPrimaries out)
{
	const Matrix3x3 bradford = {
		{  0.8951,  0.2664, -0.1614 },
		{ -0.7502,  1.7135,  0.0367 },
		{  0.0389, -0.0685,  1.0296 },
	};

	Vector3 white_in = get_white_point(in);
	Vector3 white_out = get_white_point(out);

	if (white_in == white_out)
		return Matrix3x3::identity();

	Vector3 rgb_in = bradford * white_in;
	Vector3 rgb_out = bradford * white_out;

	Matrix3x3 m{};
	m[0][0] = rgb_out[0] / rgb_in[0];
	m[1][1] = rgb_out[1] / rgb_in[1];
	m[2][2] = rgb_out[2] / rgb_in[2];

	return inverse(bradford) * m * bradford;
}

} // namespace zimg::colorspace
