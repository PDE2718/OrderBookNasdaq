#pragma once

#include "frame_reader.hpp"
#include "messages.hpp"

#include <string>

namespace itch {

std::string format_message(const MessageBodyView& view, const Message& message, u64 index);

}  // namespace itch
