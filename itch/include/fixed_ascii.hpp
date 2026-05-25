#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace itch {

template <std::size_t N>
struct FixedAscii {
  std::array<char, N> bytes{};

  std::string_view raw() const {
    return std::string_view(bytes.data(), N);
  }

  std::string_view trimmed() const {
    std::size_t end = N;
    while (end > 0 && bytes[end - 1] == ' ') {
      --end;
    }
    return std::string_view(bytes.data(), end);
  }
};

using Stock = FixedAscii<8>;
using MPID = FixedAscii<4>;
using Attribution = FixedAscii<4>;
using Reason = FixedAscii<4>;
using IssueSubType = FixedAscii<2>;

}  // namespace itch

