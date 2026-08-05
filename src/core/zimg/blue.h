#pragma once

#ifndef ZIMG_DEPTH_BLUE_H_
#define ZIMG_DEPTH_BLUE_H_

#include <cstdint>

namespace zimg::depth {

constexpr unsigned BLUE_NOISE_LEN = 64;
constexpr unsigned BLUE_NOISE_SCALE = 255;

extern const uint8_t blue_noise_table[BLUE_NOISE_LEN][BLUE_NOISE_LEN];

} // namespace zimg::depth

#endif // ZIMG_DEPTH_BLUE_H_
