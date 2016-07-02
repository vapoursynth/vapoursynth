#include "p2p.h"

namespace P2P_NAMESPACE {

namespace {

template <class Endian>
void unpack_v210(const void *src, void * const dst[4], unsigned left, unsigned right)
{
	const unsigned lsb_10b = 0x3FF;

	const uint32_t *src_p = static_cast<const uint32_t *>(src);
	uint16_t *dst_p[3] = { static_cast<uint16_t *>(dst[0]), static_cast<uint16_t *>(dst[1]), static_cast<uint16_t *>(dst[2]) };

	// v210 packs 6 pixels in 4 DWORDs.
	left = left - (left % 6);

	// Adjust pointers.
	src_p += left * 4 / 6;
	dst_p[C_Y] += left;
	dst_p[C_U] += left / 2;
	dst_p[C_V] += left / 2;

	for (unsigned i = left; i < right; i += 6) {
		uint32_t w0 = detail::endian_swap<Endian>(*src_p++);
		uint32_t w1 = detail::endian_swap<Endian>(*src_p++);
		uint32_t w2 = detail::endian_swap<Endian>(*src_p++);
		uint32_t w3 = detail::endian_swap<Endian>(*src_p++);

		*dst_p[C_U]++ = static_cast<uint16_t>((w0 >> 0) & lsb_10b);
		*dst_p[C_Y]++ = static_cast<uint16_t>((w0 >> 10) & lsb_10b);
		*dst_p[C_V]++ = static_cast<uint16_t>((w0 >> 20) & lsb_10b);
		*dst_p[C_Y]++ = static_cast<uint16_t>((w1 >> 0) & lsb_10b);

		*dst_p[C_U]++ = static_cast<uint16_t>((w1 >> 10) & lsb_10b);
		*dst_p[C_Y]++ = static_cast<uint16_t>((w1 >> 20) & lsb_10b);
		*dst_p[C_V]++ = static_cast<uint16_t>((w2 >> 0) & lsb_10b);
		*dst_p[C_Y]++ = static_cast<uint16_t>((w2 >> 10) & lsb_10b);

		*dst_p[C_U]++ = static_cast<uint16_t>((w2 >> 20) & lsb_10b);
		*dst_p[C_Y]++ = static_cast<uint16_t>((w3 >> 0) & lsb_10b);
		*dst_p[C_V]++ = static_cast<uint16_t>((w3 >> 10) & lsb_10b);
		*dst_p[C_Y]++ = static_cast<uint16_t>((w3 >> 20) & lsb_10b);
	}
}

template <class Endian>
void pack_v210(const void * const src[4], void *dst, unsigned left, unsigned right)
{
	const unsigned lsb_10b = 0x3FF;

	const uint16_t *src_p[3] = { static_cast<const uint16_t *>(src[0]), static_cast<const uint16_t *>(src[1]), static_cast<const uint16_t *>(src[2]) };
	uint32_t *dst_p = static_cast<uint32_t *>(dst);

	// v210 packs 6 pixels in 4 DWORDs.
	left = left - (left % 6);

	// Adjust pointers.
	src_p[C_Y] += left;
	src_p[C_U] += left / 2;
	src_p[C_V] += left / 2;
	dst_p += left * 4 / 6;

	for (unsigned i = left; i < right; i += 6) {
		uint32_t w0 = 0;
		uint32_t w1 = 0;
		uint32_t w2 = 0;
		uint32_t w3 = 0;

		w0 |= static_cast<uint32_t>(*src_p[C_U]++ & lsb_10b) << 0;
		w0 |= static_cast<uint32_t>(*src_p[C_Y]++ & lsb_10b) << 10;
		w0 |= static_cast<uint32_t>(*src_p[C_V]++ & lsb_10b) << 20;
		w1 |= static_cast<uint32_t>(*src_p[C_Y]++ & lsb_10b) << 0;

		w1 |= static_cast<uint32_t>(*src_p[C_U]++ & lsb_10b) << 10;
		w1 |= static_cast<uint32_t>(*src_p[C_Y]++ & lsb_10b) << 20;
		w2 |= static_cast<uint32_t>(*src_p[C_V]++ & lsb_10b) << 0;
		w2 |= static_cast<uint32_t>(*src_p[C_Y]++ & lsb_10b) << 10;

		w2 |= static_cast<uint32_t>(*src_p[C_U]++ & lsb_10b) << 20;
		w3 |= static_cast<uint32_t>(*src_p[C_Y]++ & lsb_10b) << 0;
		w3 |= static_cast<uint32_t>(*src_p[C_V]++ & lsb_10b) << 10;
		w3 |= static_cast<uint32_t>(*src_p[C_Y]++ & lsb_10b) << 20;

		*dst_p++ = detail::endian_swap<Endian>(w0);
		*dst_p++ = detail::endian_swap<Endian>(w1);
		*dst_p++ = detail::endian_swap<Endian>(w2);
		*dst_p++ = detail::endian_swap<Endian>(w3);
	}
}

} // namespace


void packed_to_planar<packed_v210_be>::unpack(const void *src, void * const dst[4], unsigned left, unsigned right)
{
	unpack_v210<big_endian_t>(src, dst, left, right);
}

void packed_to_planar<packed_v210_le>::unpack(const void *src, void * const dst[4], unsigned left, unsigned right)
{
	unpack_v210<little_endian_t>(src, dst, left, right);
}

void planar_to_packed<packed_v210_be>::pack(const void * const src[4], void *dst, unsigned left, unsigned right)
{
	pack_v210<big_endian_t>(src, dst, left, right);
}

void planar_to_packed<packed_v210_le>::pack(const void * const src[4], void *dst, unsigned left, unsigned right)
{
	pack_v210<little_endian_t>(src, dst, left, right);
}

} // namespace p2p
