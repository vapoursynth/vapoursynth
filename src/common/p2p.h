#ifndef P2P_H_
#define P2P_H_

#include <cstddef>
#include <cstdint>
#include <climits>
#include <type_traits>

#ifdef P2P_USER_NAMESPACE
  #define P2P_NAMESPACE P2P_USER_NAMESPACE
#else
  #define P2P_NAMESPACE p2p
#endif

#ifdef _WIN32
  #define P2P_LITTLE_ENDIAN
  #include <stdlib.h>
#else
  #include <endian.h>
  #if __BYTE_ORDER == __BIG_ENDIAN
    #define P2P_BIG_ENDIAN
  #elif __BYTE_ORDER == __LITTLE_ENDIAN
    #define P2P_LITTLE_ENDIAN
  #endif
#endif

static_assert(CHAR_BIT == 8, "8-bit char required");

namespace P2P_NAMESPACE {

// Tag types for endian.
struct little_endian_t {};
struct big_endian_t {};

#if defined(P2P_BIG_ENDIAN)
  typedef big_endian_t native_endian_t;
#elif defined(P2P_LITTLE_ENDIAN)
  typedef little_endian_t native_endian_t;
#else
  #error wrong endian
#endif

#undef P2P_BIG_ENDIAN
#undef P2P_LITTLE_ENDIAN

namespace detail {

// Size of object in bits.
template <class T>
struct bit_size {
	static const size_t value = sizeof(T) * CHAR_BIT;
};


// Make integers from bytes.
constexpr uint16_t make_u16(uint8_t a, uint8_t b)
{
	return (a << 8) | b;
}

constexpr uint32_t make_u32(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	return (static_cast<uint32_t>(make_u16(a, b)) << 16) | make_u16(c, d);
}

constexpr uint64_t make_u64(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e, uint8_t f, uint8_t g, uint8_t h)
{
	return (static_cast<uint64_t>(make_u32(a, b, c, d)) << 32) | make_u32(e, f, g, h);
}

template <class T>
constexpr uint8_t get_u8(T x, unsigned i)
{
	return static_cast<uint8_t>((x >> (bit_size<T>::value - 8 * i - 8)) & 0xFF);
}


// Fake 24 and 48-bit integers.
struct uint24 {
	uint8_t x[3];

	uint24() = default;

	constexpr uint24(uint8_t a, uint8_t b, uint8_t c) : x{ a, b, c }
	{
	}

	template <class T = native_endian_t>
	explicit constexpr uint24(uint32_t val,
	                          typename std::enable_if<std::is_same<T, big_endian_t>::value>::type * = 0) :
		x{ get_u8(val, 1), get_u8(val, 2), get_u8(val, 3) }
	{
	}

	template <class T = native_endian_t>
	explicit constexpr uint24(uint32_t val,
	                          typename std::enable_if<std::is_same<T, little_endian_t>::value>::type * = 0) :
		x{ get_u8(val, 3), get_u8(val, 2), get_u8(val, 1) }
	{
	}

	constexpr operator uint32_t() const { return to_u32(); }

	template <class T = native_endian_t,
	          typename std::enable_if<std::is_same<T, big_endian_t>::value>::type * = nullptr>
	constexpr uint32_t to_u32() const
	{
		return make_u32(0, x[0], x[1], x[2]);
	}

	template <class T = native_endian_t,
	          typename std::enable_if<std::is_same<T, little_endian_t>::value>::type * = nullptr>
	constexpr uint32_t to_u32() const
	{
		return make_u32(0, x[2], x[1], x[0]);
	}
};

struct uint48 {
	uint8_t x[6];

	uint48() = default;

	constexpr uint48(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e, uint8_t f) : x{ a, b, c, d, e, f }
	{
	}

	template <class T = native_endian_t>
	explicit constexpr uint48(uint64_t val,
	                          typename std::enable_if<std::is_same<T, big_endian_t>::value>::type * = 0) :
		x{ get_u8(val, 2), get_u8(val, 3), get_u8(val, 4), get_u8(val, 5), get_u8(val, 6), get_u8(val, 7) }
	{
	}

	template <class T = native_endian_t>
	explicit constexpr uint48(uint64_t val,
	                          typename std::enable_if<std::is_same<T, little_endian_t>::value>::type * = 0) :
		x{ get_u8(val, 7), get_u8(val, 6), get_u8(val, 5), get_u8(val, 4), get_u8(val, 3), get_u8(val, 2) }
	{
	}

	constexpr operator uint64_t() const { return to_u64(); }

	template <class T = native_endian_t,
	          typename std::enable_if<std::is_same<T, big_endian_t>::value>::type * = nullptr>
	constexpr uint64_t to_u64() const
	{
		return make_u64(0, 0, x[0], x[1], x[2], x[3], x[4], x[5]);
	}

	template <class T = native_endian_t,
	          typename std::enable_if<std::is_same<T, little_endian_t>::value>::type * = nullptr>
	constexpr uint64_t to_u64() const
	{
		return make_u64(0, 0, x[5], x[4], x[3], x[2], x[1], x[0]);
	}
};

static_assert(std::is_pod<uint24>::value, "uint24 must be POD");
static_assert(std::is_pod<uint48>::value, "uint48 must be POD");
static_assert(sizeof(uint24) == 3, "uint24 must not have padding");
static_assert(sizeof(uint48) == 6, "uint48 must not have padding");


// Endian conversions.
template <class Endian, class T,
          typename std::enable_if<std::is_same<Endian, native_endian_t>::value>::type * = nullptr>
T endian_swap(T x)
{
	return x;
}

template <class Endian,
          typename std::enable_if<!std::is_same<Endian, native_endian_t>::value>::type * = nullptr>
uint16_t endian_swap(uint16_t x)
{
#ifdef _WIN32
	return _byteswap_ushort(x);
#else
	return __builtin_bswap16(x);
#endif
}

template <class Endian,
          typename std::enable_if<!std::is_same<Endian, native_endian_t>::value>::type * = nullptr>
uint32_t endian_swap(uint32_t x)
{
#ifdef _WIN32
	return _byteswap_ulong(x);
#else
	return __builtin_bswap32(x);
#endif
}

template <class Endian,
          typename std::enable_if<!std::is_same<Endian, native_endian_t>::value>::type * = nullptr>
uint64_t endian_swap(uint64_t x)
{
#ifdef _WIN32
	return _byteswap_uint64(x);
#else
	return __builtin_bswap64(x);
#endif
}

template <class Endian,
          typename std::enable_if<!std::is_same<Endian, native_endian_t>::value>::type * = nullptr>
uint24 endian_swap(uint24 x)
{
	return{ x.x[2], x.x[1], x.x[0] };
}

template <class Endian,
          typename std::enable_if<!std::is_same<Endian, native_endian_t>::value>::type * = nullptr>
uint48 endian_swap(uint48 x)
{
	return{ x.x[5], x.x[4], x.x[3], x.x[2], x.x[1], x.x[0] };
}


// Treat u32 as array of u8.
struct mask4 {
	uint32_t x;

	constexpr uint8_t operator[](unsigned i) const { return get_u8(x, i); }
	constexpr bool contains(unsigned val) const
	{
		return (*this)[0] == val || (*this)[1] == val || (*this)[2] == val || (*this)[3] == val;
	}
};


// Native integer type for arithmetic.
template <class T>
struct numeric_type {
	typedef T type;
};

template <>
struct numeric_type<uint24> {
	typedef uint32_t type;
};

template <>
struct numeric_type<uint48> {
	typedef uint64_t type;
};

} // namespace detail


enum {
	C_Y = 0,
	C_U = 1,
	C_V = 2,
	C_R = 0,
	C_G = 1,
	C_B = 2,
	C_A = 3,
	C__ = 0xFF,
};


// Packed integer constants for template parameters.
constexpr uint32_t make_mask(uint8_t x)
{
	return detail::make_u32(x, x, x, x);
}

constexpr uint32_t make_mask(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	return detail::make_u32(a, b, c, d);
}


// Select type by endian.
template <class Big, class Little>
struct endian_select {
	typedef typename std::conditional<std::is_same<native_endian_t, big_endian_t>::value, Big, Little>::type type;
};


template <class Planar,
          class Packed,
          class Endian,
          unsigned PelPerPack,
          unsigned Subsampling,
          uint32_t ComponentMask,
          uint32_t ShiftMask,
          uint32_t DepthMask>
struct pack_traits {
	static_assert(std::is_pod<Planar>::value, "must be POD");
	static_assert(std::is_pod<Packed>::value, "must be POD");

	typedef Planar planar_type;
	typedef Packed packed_type;
	typedef Endian endian;

	static const unsigned pel_per_pack = PelPerPack;
	static const unsigned subsampling = Subsampling;

	static constexpr detail::mask4 component_mask{ ComponentMask };
	static constexpr detail::mask4 shift_mask{ ShiftMask };
	static constexpr detail::mask4 depth_mask{ DepthMask };
};

template <class Planar, class Packed, class Endian, unsigned PelPerPack, unsigned Subsampling, uint32_t ComponentMask, uint32_t ShiftMask, uint32_t DepthMask>
constexpr detail::mask4 pack_traits<Planar, Packed, Endian, PelPerPack, Subsampling, ComponentMask, ShiftMask, DepthMask>::component_mask;

template <class Planar, class Packed, class Endian, unsigned PelPerPack, unsigned Subsampling, uint32_t ComponentMask, uint32_t ShiftMask, uint32_t DepthMask>
constexpr detail::mask4 pack_traits<Planar, Packed, Endian, PelPerPack, Subsampling, ComponentMask, ShiftMask, DepthMask>::shift_mask;

template <class Planar, class Packed, class Endian, unsigned PelPerPack, unsigned Subsampling, uint32_t ComponentMask, uint32_t ShiftMask, uint32_t DepthMask>
constexpr detail::mask4 pack_traits<Planar, Packed, Endian, PelPerPack, Subsampling, ComponentMask, ShiftMask, DepthMask>::depth_mask;


// Base template for 4:4:4 packings.
//
// Much literature treats 4:4:4 triplets as single machine words, implying a
// reversed component order on LE and BE.
//
// The _be and _le templates accept a component mask beginning from the MSB of
// the packed word to accomodate this.
template <class Planar, class Packed, uint32_t ComponentMask>
using byte_packed_444_be = pack_traits<
	Planar, Packed, big_endian_t, 1, 0,
	ComponentMask,
	make_mask(3, 2, 1, 0) * detail::bit_size<Planar>::value,
	make_mask(detail::bit_size<Planar>::value)>;

template <class Planar, class Packed, uint32_t ComponentMask>
using byte_packed_444_le = pack_traits<
	Planar, Packed, little_endian_t, 1, 0,
	ComponentMask,
	make_mask(3, 2, 1, 0) * detail::bit_size<Planar>::value,
	make_mask(detail::bit_size<Planar>::value)>;

// Common 444 packings.
using packed_rgb24_be = byte_packed_444_be<uint8_t, detail::uint24, make_mask(C__, C_R, C_G, C_B)>;
using packed_rgb24_le = byte_packed_444_le<uint8_t, detail::uint24, make_mask(C__, C_R, C_G, C_B)>;
using packed_rgb24 = endian_select<packed_rgb24_be, packed_rgb24_le>::type;

using packed_argb32_be = byte_packed_444_be<uint8_t, uint32_t, make_mask(C_A, C_R, C_G, C_B)>;
using packed_argb32_le = byte_packed_444_le<uint8_t, uint32_t, make_mask(C_A, C_R, C_G, C_B)>;
using packed_argb32 = endian_select<packed_argb32_be, packed_argb32_le>::type;

using packed_ayuv_be = packed_argb32_be;
using packed_ayuv_le = packed_argb32_le;
using packed_ayuv = packed_argb32;

using packed_rgb48_be = byte_packed_444_be<uint16_t, detail::uint48, make_mask(C__, C_R, C_G, C_B)>;
using packed_rgb48_le = byte_packed_444_le<uint16_t, detail::uint48, make_mask(C__, C_R, C_G, C_B)>;
using packed_rgb48 = endian_select<packed_rgb48_be, packed_rgb48_le>::type;

using packed_argb64_be = byte_packed_444_be<uint16_t, uint64_t, make_mask(C_A, C_R, C_G, C_B)>;
using packed_argb64_le = byte_packed_444_le<uint16_t, uint64_t, make_mask(C_A, C_R, C_G, C_B)>;
using packed_argb64 = endian_select<packed_argb64_be, packed_argb64_le>::type;

// D3D A2R10G10B10.
using packed_rgb30_be = pack_traits<
	uint16_t, uint32_t, big_endian_t, 1, 0,
	make_mask(C_A, C_R, C_G, C_B),
	make_mask(30, 20, 10, 0),
	make_mask(2, 10, 10, 10)>;
using packed_rgb30_le = pack_traits<
	uint16_t, uint32_t, little_endian_t, 1, 0,
	make_mask(C_A, C_R, C_G, C_B),
	make_mask(30, 20, 10, 0),
	make_mask(2, 10, 10, 10)>;
using packed_rgb30 = endian_select<packed_rgb30_be, packed_rgb30_le>::type;

// MS Y410 and Y416 formats.
using packed_y410_be = pack_traits<
	uint16_t, uint32_t, big_endian_t, 1, 0,
	make_mask(C_A, C_V, C_Y, C_U),
	make_mask(30, 20, 10, 0),
	make_mask(2, 10, 10, 10)>;
using packed_y410_le = pack_traits<
	uint16_t, uint32_t, little_endian_t, 1, 0,
	make_mask(C_A, C_V, C_Y, C_U),
	make_mask(30, 20, 10, 0),
	make_mask(2, 10, 10, 10)>;
using packed_y410 = typename endian_select<packed_y410_be, packed_y410_le>::type;

using packed_y416_be = byte_packed_444_be<uint16_t, uint64_t, make_mask(C_A, C_V, C_Y, C_U)>;
using packed_y416_le = byte_packed_444_le<uint16_t, uint64_t, make_mask(C_A, C_V, C_Y, C_U)>;
using packed_y416 = endian_select<packed_y416_be, packed_y416_le>::type;


// Base template for YUY2-like 4:2:2 packings.
//
// The component order in both BE and LE is the same. Only the bytes of the
// individual component words are reversed.
//
// The _be and _le templates accept a component mask beginning from the low
// memory address of the packed word to accomodate this.
template <class Planar, class Packed, uint32_t ComponentMask, unsigned ExtraShift = 0>
using byte_packed_422_be = pack_traits<
	Planar, Packed, big_endian_t, 2, 1,
	ComponentMask,
	make_mask(3, 2, 1, 0) * detail::bit_size<Planar>::value + make_mask(ExtraShift),
	make_mask(detail::bit_size<Planar>::value) - make_mask(ExtraShift)>;

template <class Planar, class Packed, uint32_t ComponentMask, unsigned ExtraShift = 0>
using byte_packed_422_le = pack_traits<
	Planar, Packed, little_endian_t, 2, 1,
	ComponentMask,
	make_mask(0, 1, 2, 3) * detail::bit_size<Planar>::value + make_mask(ExtraShift),
	make_mask(detail::bit_size<Planar>::value) - make_mask(ExtraShift)>;

// YUY2.
using packed_yuy2 = byte_packed_422_be<uint8_t, uint32_t, make_mask(C_Y, C_U, C_Y, C_V)>;
using packed_uyvy = byte_packed_422_be<uint8_t, uint32_t, make_mask(C_U, C_Y, C_V, C_Y)>;

// MS Y210 and Y216 formats.
using packed_y210_be = byte_packed_422_be<uint16_t, uint64_t, make_mask(C_Y, C_U, C_Y, C_V), 6>;
using packed_y210_le = byte_packed_422_le<uint16_t, uint64_t, make_mask(C_Y, C_U, C_Y, C_V), 6>;
using packed_y210 = endian_select<packed_y210_be, packed_y210_le>::type;

using packed_y216_be = byte_packed_422_be<uint16_t, uint64_t, make_mask(C_Y, C_U, C_Y, C_V)>;
using packed_y216_le = byte_packed_422_le<uint16_t, uint16_t, make_mask(C_Y, C_U, C_Y, C_V)>;
using packed_y216 = endian_select<packed_y216_be, packed_y216_le>::type;

// Apple v210 format. Handled by special-case code. Only the LE ordering is found in Qt files.
struct packed_v210_be {};
struct packed_v210_le {};
using packed_v210 = endian_select<packed_v210_le, packed_v210_be>::type;

// Apple v216 format. Only the LE ordering is found in Qt files.
using packed_v216_be = byte_packed_422_be<uint16_t, uint64_t, make_mask(C_U, C_Y, C_V, C_Y)>;
using packed_v216_le = byte_packed_422_le<uint16_t, uint64_t, make_mask(C_U, C_Y, C_V, C_Y)>;
using packed_v216 = endian_select<packed_v216_le, packed_v216_be>::type;


// Base template for chroma-interleaved half packings.
//
// The literature treats UV pairs as single machine words, implying a reversed
// component order between BE and LE.
template <class Planar, class Packed, unsigned ExtraShift = 0>
using byte_packed_nv_be = pack_traits<
	Planar, Packed, big_endian_t, 2, 1,
	make_mask(C__, C__, C_V, C_U),
	make_mask(0, 0, 1, 0) * detail::bit_size<Planar>::value + make_mask(ExtraShift),
	make_mask(detail::bit_size<Planar>::value) - make_mask(ExtraShift)>;

template <class Planar, class Packed, unsigned ExtraShift = 0>
using byte_packed_nv_le = pack_traits<
	Planar, Packed, little_endian_t, 2, 1,
	make_mask(C__, C__, C_V, C_U),
	make_mask(0, 0, 1, 0) * detail::bit_size<Planar>::value + make_mask(ExtraShift),
	make_mask(detail::bit_size<Planar>::value) - make_mask(ExtraShift)>;

using packed_nv12_be = byte_packed_nv_be<uint8_t, uint16_t>; // AKA NV21.
using packed_nv12_le = byte_packed_nv_le<uint8_t, uint16_t>;
using packed_nv12 = endian_select<packed_nv12_be, packed_nv12_le>::type;

// MS P010, P016, P210, and P216 formats.
using packed_p010_be = byte_packed_nv_be<uint16_t, uint32_t, 6>;
using packed_p010_le = byte_packed_nv_le<uint16_t, uint32_t, 6>;
using packed_p010 = endian_select<packed_p010_be, packed_p010_le>::type;

using packed_p016_be = byte_packed_nv_be<uint16_t, uint32_t>;
using packed_p016_le = byte_packed_nv_le<uint16_t, uint32_t>;
using packed_p016 = endian_select<packed_p016_be, packed_p016_le>::type;

using packed_p210_be = packed_p010_be;
using packed_p210_le = packed_p010_le;
using packed_p210 = packed_p010;

using packed_p216_be = packed_p016_be;
using packed_p216_le = packed_p016_le;
using packed_p216 = packed_p016;


// Conversions.
template <class Traits>
class packed_to_planar {
	typedef typename Traits::planar_type planar_type;
	typedef typename Traits::packed_type packed_type;
	typedef typename detail::numeric_type<packed_type>::type numeric_type;

	typedef typename Traits::endian endian;

	static numeric_type get_mask(unsigned c)
	{
		return ~static_cast<numeric_type>(0) >> (detail::bit_size<numeric_type>::value - Traits::depth_mask[c]);
	}

	static planar_type get_component(numeric_type x, unsigned c)
	{
		return static_cast<planar_type>((x >> Traits::shift_mask[c]) & get_mask(c));
	}
public:
	static void unpack(const void *src, void * const dst[4], unsigned left, unsigned right)
	{
		const packed_type *src_p = static_cast<const packed_type *>(src);
		planar_type *dst_p[4] = {
			static_cast<planar_type *>(dst[0]), static_cast<planar_type *>(dst[1]),
			static_cast<planar_type *>(dst[2]), static_cast<planar_type *>(dst[3]),
		};
		bool alpha_enabled = dst[C_A] != nullptr;

		// Adjust pointers.
		src_p += left / Traits::pel_per_pack;
		dst_p[0] += Traits::component_mask.contains(0) ? left : 0;
		dst_p[1] += Traits::component_mask.contains(1) ? (left >> Traits::subsampling) : 0;
		dst_p[2] += Traits::component_mask.contains(2) ? (left >> Traits::subsampling) : 0;
		dst_p[3] += Traits::component_mask.contains(3) ? left : 0;

#define P2P_COMPONENT_ENABLED(c) ((Traits::component_mask[c] != C__) && (Traits::component_mask[c] != C_A || alpha_enabled))
		for (unsigned i = left; i < right; i += Traits::pel_per_pack) {
			numeric_type x = detail::endian_swap<endian>(*src_p++);

			if (P2P_COMPONENT_ENABLED(0))
				*dst_p[Traits::component_mask[0]]++ = get_component(x, 0);
			if (P2P_COMPONENT_ENABLED(1))
				*dst_p[Traits::component_mask[1]]++ = get_component(x, 1);
			if (P2P_COMPONENT_ENABLED(2))
				*dst_p[Traits::component_mask[2]]++ = get_component(x, 2);
			if (P2P_COMPONENT_ENABLED(3))
				*dst_p[Traits::component_mask[3]]++ = get_component(x, 3);
		}
#undef P2P_COMPONENT_ENABLED
	}
};

template <class Traits>
class planar_to_packed {
	typedef typename Traits::planar_type planar_type;
	typedef typename Traits::packed_type packed_type;
	typedef typename detail::numeric_type<packed_type>::type numeric_type;

	typedef typename Traits::endian endian;

	static numeric_type get_mask(unsigned c)
	{
		return ~static_cast<numeric_type>(0) >> (detail::bit_size<numeric_type>::value - Traits::depth_mask[c]);
	}

	static numeric_type get_component(planar_type x, unsigned c)
	{
		return (static_cast<numeric_type>(x) & get_mask(c)) << Traits::shift_mask[c];
	}
public:
	static void pack(const void * const src[4], void *dst, unsigned left, unsigned right)
	{
		const planar_type *src_p[4] = {
			static_cast<const planar_type *>(src[0]), static_cast<const planar_type *>(src[1]),
			static_cast<const planar_type *>(src[2]), static_cast<const planar_type *>(src[3]),
		};
		packed_type *dst_p = static_cast<packed_type *>(dst);
		bool alpha_enabled = src[C_A] != nullptr;

		// Adjust pointers.
		src_p[0] += Traits::component_mask.contains(0) ? left : 0;
		src_p[1] += Traits::component_mask.contains(1) ? (left >> Traits::subsampling) : 0;
		src_p[2] += Traits::component_mask.contains(2) ? (left >> Traits::subsampling) : 0;
		src_p[3] += Traits::component_mask.contains(3) ? left : 0;
		dst_p += left / Traits::pel_per_pack;

#define P2P_COMPONENT_ENABLED(c) ((Traits::component_mask[c] != C__) && (Traits::component_mask[c] != C_A || alpha_enabled))
		for (unsigned i = left; i < right; i += Traits::pel_per_pack) {
			numeric_type x = 0;

			if (P2P_COMPONENT_ENABLED(0))
				x |= get_component(*src_p[Traits::component_mask[0]]++, 0);
			if (P2P_COMPONENT_ENABLED(1))
				x |= get_component(*src_p[Traits::component_mask[1]]++, 1);
			if (P2P_COMPONENT_ENABLED(2))
				x |= get_component(*src_p[Traits::component_mask[2]]++, 2);
			if (P2P_COMPONENT_ENABLED(3))
				x |= get_component(*src_p[Traits::component_mask[3]]++, 3);

			*dst_p++ = detail::endian_swap<endian>(static_cast<packed_type>(x));
		}
	}
#undef P2P_COMPONENT_ENABLED
};

template <>
class packed_to_planar<packed_v210_le> {
public:
	static void unpack(const void *src, void * const dst[4], unsigned left, unsigned right);
};

template <>
class packed_to_planar<packed_v210_be> {
public:
	static void unpack(const void *src, void * const dst[4], unsigned left, unsigned right);
};

template <>
class planar_to_packed<packed_v210_le> {
public:
	static void pack(const void * const src[4], void *dst, unsigned left, unsigned right);
};

template <>
class planar_to_packed<packed_v210_be> {
public:
	static void pack(const void * const src[4], void *dst, unsigned left, unsigned right);
};

} // namespace p2p

#endif // P2P_H_
