#pragma once

#ifndef ZIMG_EXCEPT_H_
#define ZIMG_EXCEPT_H_

#include <stdexcept>

namespace zimg {
namespace error {

class Exception : private std::runtime_error {
public:
	Exception() : std::runtime_error{ "" } {}

	using std::runtime_error::runtime_error;

	virtual ~Exception() = default;

	using std::runtime_error::what;
};

#define DECLARE_EXCEPTION(x, base) class x : public base { public: using base::base; };

DECLARE_EXCEPTION(UnknownError, Exception)
DECLARE_EXCEPTION(InternalError, Exception)

DECLARE_EXCEPTION(OutOfMemory, Exception)
DECLARE_EXCEPTION(UserCallbackFailed, Exception)

DECLARE_EXCEPTION(LogicError, Exception)
DECLARE_EXCEPTION(GreyscaleSubsampling, LogicError)
DECLARE_EXCEPTION(ColorFamilyMismatch, LogicError)
DECLARE_EXCEPTION(ImageNotDivisible, LogicError)
DECLARE_EXCEPTION(BitDepthOverflow, LogicError)

DECLARE_EXCEPTION(IllegalArgument, Exception)
DECLARE_EXCEPTION(EnumOutOfRange, IllegalArgument)
DECLARE_EXCEPTION(InvalidImageSize, IllegalArgument)

DECLARE_EXCEPTION(UnsupportedOperation, Exception)
DECLARE_EXCEPTION(UnsupportedSubsampling, UnsupportedOperation)
DECLARE_EXCEPTION(NoColorspaceConversion, UnsupportedOperation)
DECLARE_EXCEPTION(ResamplingNotAvailable, UnsupportedOperation)
DECLARE_EXCEPTION(NoFieldParityConversion, UnsupportedOperation)

#undef DECLARE_EXCEPTION

template <class T>
[[noreturn]] void throw_()
{
	throw T{};
}

template <class T>
[[noreturn]] void throw_(const char *msg)
{
	throw T{ msg };
}

} // namespace error
} // namespace zimg

#endif // ZIMG_EXCEPT_H_
