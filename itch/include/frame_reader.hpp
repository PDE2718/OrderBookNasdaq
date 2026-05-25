#pragma once

#include "types.hpp"

#include <fstream>
#include <string>
#include <vector>

namespace itch {

struct MessageBodyView {
  u64 offset = 0;
  u16 length = 0;
  const u8* body = nullptr;
};

class FrameReader {
 public:
  explicit FrameReader(const std::string& path);

  bool next(MessageBodyView& out);
  u64 offset() const { return offset_; }

 private:
  std::ifstream input_;
  std::vector<u8> buffer_;
  u64 offset_ = 0;
};

}  // namespace itch
