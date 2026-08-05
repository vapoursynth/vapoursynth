#pragma once

#ifndef ZIMG_COLORSPACE_COLORSPACE_H_
#define ZIMG_COLORSPACE_COLORSPACE_H_

#include <memory>

namespace graphengine {
class Filter;
}

namespace zimg {
enum class CPUClass;
}

namespace zimg::colorspace {

enum class MatrixCoefficients {
	UNSPECIFIED,
	RGB,
	REC_601,
	REC_709,
	FCC,
	SMPTE_240M,
	YCGCO,
	REC_2020_NCL,
	REC_2020_CL,
	CHROMATICITY_DERIVED_NCL,
	CHROMATICITY_DERIVED_CL,
	REC_2100_LMS,
	REC_2100_ICTCP,
};

enum class TransferCharacteristics {
	UNSPECIFIED,
	LINEAR,
	LOG_100,
	LOG_316,
	REC_709,
	REC_470_M,
	REC_470_BG,
	SMPTE_240M,
	XVYCC,
	REC_1361,
	SRGB,
	ST_2084,
	ST_428,
	ARIB_B67,
};

enum class ColorPrimaries {
	UNSPECIFIED,
	REC_470_M,
	REC_470_BG,
	SMPTE_C,
	REC_709,
	FILM,
	REC_2020,
	XYZ,
	DCI_P3,
	DCI_P3_D65,
	EBU_3213_E,
};

/**
 * Definition of a working colorspace.
 */
struct ColorspaceDefinition {
	MatrixCoefficients matrix;
	TransferCharacteristics transfer;
	ColorPrimaries primaries;

	// Helper functions to create modified colorspaces.
	constexpr ColorspaceDefinition to(MatrixCoefficients matrix_) const noexcept
	{
		return{ matrix_, transfer, primaries };
	}

	constexpr ColorspaceDefinition to(TransferCharacteristics transfer_) const noexcept
	{
		return{ matrix, transfer_, primaries };
	}

	constexpr ColorspaceDefinition to(ColorPrimaries primaries_) const noexcept
	{
		return{ matrix, transfer, primaries_ };
	}

	constexpr ColorspaceDefinition to_rgb() const noexcept
	{
		return to(MatrixCoefficients::RGB);
	}

	constexpr ColorspaceDefinition to_linear() const noexcept
	{
		return to(TransferCharacteristics::LINEAR);
	}
};

// Compare colorspaces by comparing each component.
constexpr bool operator==(const ColorspaceDefinition &a, const ColorspaceDefinition &b) noexcept
{
	return a.matrix == b.matrix && a.transfer == b.transfer && a.primaries == b.primaries;
}

constexpr bool operator!=(const ColorspaceDefinition &a, const ColorspaceDefinition &b) noexcept
{
	return !(a == b);
}


} // namespace zimg::colorspace

#endif
