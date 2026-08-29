#pragma once

#include <cstdint>

struct CoverImage {
  const std::uint16_t* pixels;
  std::uint16_t width;
  std::uint16_t height;
};
