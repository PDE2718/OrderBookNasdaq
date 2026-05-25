#pragma once

#include "frame_reader.hpp"
#include "messages.hpp"
#include "types.hpp"

namespace itch {

Message parse_message_body(const u8* body, u16 length, u64 offset = 0);
Message parse_message_body(const MessageBodyView& view);

}  // namespace itch
