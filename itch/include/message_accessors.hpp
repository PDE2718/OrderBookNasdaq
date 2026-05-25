#pragma once

#include "messages.hpp"

#include <optional>
#include <type_traits>

namespace itch {

inline char message_type(const Message& message) {
  return std::visit(
      [](const auto& msg) -> char {
        return msg.type;
      },
      message);
}

inline u16 message_stock_locate(const Message& message) {
  return std::visit(
      [](const auto& msg) -> u16 {
        using T = std::decay_t<decltype(msg)>;
        if constexpr (std::is_same_v<T, UnknownMessage> ||
                      std::is_same_v<T, MalformedMessage>) {
          return 0;
        } else {
          return msg.stock_locate;
        }
      },
      message);
}

inline u64 message_timestamp(const Message& message) {
  return std::visit(
      [](const auto& msg) -> u64 {
        using T = std::decay_t<decltype(msg)>;
        if constexpr (std::is_same_v<T, UnknownMessage> ||
                      std::is_same_v<T, MalformedMessage>) {
          return 0;
        } else {
          return msg.timestamp;
        }
      },
      message);
}

inline std::optional<BookMessage> to_book_message(const Message& message) {
  return std::visit(
      [](const auto& msg) -> std::optional<BookMessage> {
        using T = std::decay_t<decltype(msg)>;
        if constexpr (is_book_message_v<T>) {
          return BookMessage{msg};
        } else {
          return std::nullopt;
        }
      },
      message);
}

}  // namespace itch
