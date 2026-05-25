#include "frame_reader.hpp"

#include "endian.hpp"

#include <stdexcept>

namespace itch {

FrameReader::FrameReader(const std::string& path)
    : input_(path, std::ios::binary) {
  if (!input_) {
    throw std::runtime_error("failed to open input file: " + path);
  }
}

bool FrameReader::next(MessageBodyView& out) {
  u8 header[2] = {0, 0};
  input_.read(reinterpret_cast<char*>(header), 2);
  if (input_.eof() && input_.gcount() == 0) {
    return false;
  }
  if (input_.gcount() != 2) {
    throw std::runtime_error("truncated frame header at offset " +
                             std::to_string(offset_));
  }

  const u16 length = read_be16(header);
  buffer_.resize(length);

  if (length > 0) {
    input_.read(reinterpret_cast<char*>(buffer_.data()), length);
    if (input_.gcount() != length) {
      throw std::runtime_error("truncated frame body at offset " +
                               std::to_string(offset_));
    }
  }

  out.offset = offset_;
  out.length = length;
  out.body = buffer_.data();

  offset_ += static_cast<u64>(2) + length;
  return true;
}

}  // namespace itch
