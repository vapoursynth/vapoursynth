#pragma once

#ifndef ZIMG_COLORSPACE_COLORSPACE_PARAM_H_
#define ZIMG_COLORSPACE_COLORSPACE_PARAM_H_

#include "matrix3.h"

namespace zimg::colorspace {

enum class MatrixCoefficients;
enum class ColorPrimaries;

// Kr and Kb YUV<-->RGB constants.
constexpr double FCC_KR = 0.3;
constexpr double FCC_KB = 0.11;
constexpr double SMPTE_240M_KR = 0.212;
constexpr double SMPTE_240M_KB = 0.087;
constexpr double REC_601_KR = 0.299;
constexpr double REC_601_KB = 0.114;
constexpr double REC_709_KR = 0.2126;
constexpr double REC_709_KB = 0.0722;
constexpr double REC_2020_KR = 0.2627;
constexpr double REC_2020_KB = 0.0593;

// R, G, B primaries in XY.
constexpr double REC_470_M_PRIMARIES[3][2] = { { 0.670, 0.330 }, { 0.210, 0.710 }, { 0.140, 0.080 } };
constexpr double REC_470_BG_PRIMARIES[3][2] = { { 0.640, 0.330 }, { 0.290, 0.600 }, { 0.150, 0.060 } };
constexpr double SMPTE_C_PRIMARIES[3][2] = { { 0.630, 0.340 }, { 0.310, 0.595 }, { 0.155, 0.070 } };
constexpr double REC_709_PRIMARIES[3][2] = { { 0.640, 0.330 }, { 0.300, 0.600 }, { 0.150, 0.060 } };
constexpr double FILM_PRIMARIES[3][2] = { { 0.681, 0.319 }, { 0.243, 0.692 }, { 0.145, 0.049 } };
constexpr double REC_2020_PRIMARIES[3][2] = { { 0.708, 0.292 }, { 0.170, 0.797 }, { 0.131, 0.046 } };
constexpr double DCI_P3_PRIMARIES[3][2] = { { 0.680, 0.320 } , { 0.265, 0.690 }, { 0.150, 0.060 } };
constexpr double EBU_3213_E_PRIMARIES[3][2] = { { 0.630, 0.340 }, { 0.295, 0.605 }, { 0.155, 0.077 } };

// White points in XY.
constexpr double ILLUMINANT_C[2]   = { 0.31, 0.316 };
constexpr double ILLUMINANT_DCI[2] = { 0.314, 0.351 };
constexpr double ILLUMINANT_D65[2] = { 0.3127, 0.3290 };
constexpr double ILLUMINANT_E[2]   = { 1.0 / 3.0, 1.0 / 3.0 };

/**
 * Obtain 3x3 matrix for converting from YUV to RGB.
 *
 * @param matrix matrix coefficients
 * @return conversion function as matrix
 */
Matrix3x3 ncl_yuv_to_rgb_matrix(MatrixCoefficients matrix);

/**
 * Obtain 3x3 matrix for converting from RGB to YUV.
 *
 * @see ncl_yuv_to_rgb_matrix
 */
Matrix3x3 ncl_rgb_to_yuv_matrix(MatrixCoefficients matrix);

/**
 * Derive YUV to RGB conversion function from color primaries.
 *
 * @param primaries color primaries
 * @return conversion function as matrix
 */
Matrix3x3 ncl_yuv_to_rgb_matrix_from_primaries(ColorPrimaries primaries);

/**
 * Derive RGB to YUV conversion function from color primaries.
 *
 * @param primaries color primaries
 * @return conversion function as matrix
 */
Matrix3x3 ncl_rgb_to_yuv_matrix_from_primaries(ColorPrimaries primaries);

/**
 * Obtain 3x3 matrix for converting from ICtCp to LMS.
 *
 * @return conversion function as matrix
 */
Matrix3x3 ictcp_to_lms_matrix(TransferCharacteristics transfer);

/**
 * Obtain 3x3 matrix for converting from LMS to ICtCp.
 *
 * @see ictcp_to_lms_matrix
 */
Matrix3x3 lms_to_ictcp_matrix(TransferCharacteristics transfer);

/**
 * Obtain 3x3 matrix for converting from RGB to XYZ.
 *
 * @param primaries primaries
 * @param conversion function as matrix
 */
Matrix3x3 gamut_rgb_to_xyz_matrix(ColorPrimaries primaries);

/**
 * Obtain 3x3 matrix for converting from XYZ to RGB.
 *
 * @see gamut_rgb_to_xyz_matrix
 */
Matrix3x3 gamut_xyz_to_rgb_matrix(ColorPrimaries primaries);

/**
 * Obtain 3x3 matrix for adapting white point.
 *
 * @param in input primaries
 * @param out output primaries
 * @return adaptation function as matrix
 */
Matrix3x3 white_point_adaptation_matrix(ColorPrimaries in, ColorPrimaries out);

} // namespace zimg::colorspace

#endif // ZIMG_COLORSPACE_COLORSPACE_PARAM_H_
