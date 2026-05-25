#pragma once

#include "types.hpp"

#include <cstddef>

namespace itch
{

  inline u16 read_be16(const u8 *p)
  {
    return static_cast<u16>(
        (static_cast<u16>(p[0]) << 8) |
        (static_cast<u16>(p[1])));
  }

  inline u32 read_be32(const u8 *p)
  {
    return (static_cast<u32>(p[0]) << 24) |
           (static_cast<u32>(p[1]) << 16) |
           (static_cast<u32>(p[2]) << 8) |
           static_cast<u32>(p[3]);
  }

  inline u64 read_be48(const u8 *p)
  {
    u64 value = 0;
    for (std::size_t i = 0; i < 6; ++i)
    {
      value = (value << 8) | static_cast<u64>(p[i]);
    }
    return value;
  }

  inline u64 read_be64(const u8 *p)
  {
    u64 value = 0;
    for (std::size_t i = 0; i < 8; ++i)
    {
      value = (value << 8) | static_cast<u64>(p[i]);
    }
    return value;
  }

} // namespace itch
