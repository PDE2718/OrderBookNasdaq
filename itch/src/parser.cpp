#include "parser.hpp"

#include "endian.hpp"
#include "message_types.hpp"

#include <algorithm>

namespace itch {
namespace {

template <std::size_t N>
FixedAscii<N> fixed_ascii(const u8* p) {
  FixedAscii<N> out;
  std::copy_n(reinterpret_cast<const char*>(p), N, out.bytes.begin());
  return out;
}

char msg_type(const u8* b) { return static_cast<char>(b[0]); }
u16 locate(const u8* b) { return read_be16(b + 1); }
u16 tracking(const u8* b) { return read_be16(b + 3); }
u64 timestamp(const u8* b) { return read_be48(b + 5); }

}  // namespace

Message parse_message_body(const u8* body, u16 length, u64 offset) {
  if (body == nullptr || length == 0) {
    return MalformedMessage{offset, '\0', length, 0,
                            ParseErrorKind::EmptyBody};
  }

  const auto* b = body;
  const char type = static_cast<char>(b[0]);
  const u16 expected = expected_length(type);

  if (expected == kUnknownLength) {
    return UnknownMessage{offset, type, length, ParseErrorKind::UnknownType};
  }
  if (length != expected) {
    return MalformedMessage{offset, type, length, expected,
                            ParseErrorKind::LengthMismatch};
  }

  switch (type) {
    case 'S':
      return SystemEvent{timestamp(b), locate(b), tracking(b), msg_type(b),
                         static_cast<char>(b[11])};

    case 'R':
      return StockDirectory{timestamp(b),
                            read_be32(b + 21),
                            read_be32(b + 34),
                            fixed_ascii<8>(b + 11),
                            fixed_ascii<2>(b + 27),
                            locate(b),
                            tracking(b),
                            msg_type(b),
                            static_cast<char>(b[19]),
                            static_cast<char>(b[20]),
                            static_cast<char>(b[25]),
                            static_cast<char>(b[26]),
                            static_cast<char>(b[29]),
                            static_cast<char>(b[30]),
                            static_cast<char>(b[31]),
                            static_cast<char>(b[32]),
                            static_cast<char>(b[33]),
                            static_cast<char>(b[38])};

    case 'H':
      return StockTradingAction{timestamp(b), fixed_ascii<8>(b + 11),
                                fixed_ascii<4>(b + 21), locate(b),
                                tracking(b), msg_type(b),
                                static_cast<char>(b[19]),
                                static_cast<char>(b[20])};

    case 'Y':
      return RegSHORestriction{timestamp(b), fixed_ascii<8>(b + 11),
                               locate(b), tracking(b), msg_type(b),
                               static_cast<char>(b[19])};

    case 'L':
      return MarketParticipantPosition{timestamp(b),
                                       fixed_ascii<8>(b + 15),
                                       fixed_ascii<4>(b + 11),
                                       locate(b),
                                       tracking(b),
                                       msg_type(b),
                                       static_cast<char>(b[23]),
                                       static_cast<char>(b[24]),
                                       static_cast<char>(b[25])};

    case 'V':
      return MWCBDeclineLevel{timestamp(b), read_be64(b + 11),
                              read_be64(b + 19), read_be64(b + 27),
                              locate(b), tracking(b), msg_type(b)};

    case 'W':
      return MWCBStatus{timestamp(b), locate(b), tracking(b), msg_type(b),
                        static_cast<char>(b[11])};

    case 'K':
      return IPOQuotingPeriodUpdate{timestamp(b), read_be32(b + 19),
                                    read_be32(b + 24),
                                    fixed_ascii<8>(b + 11), locate(b),
                                    tracking(b), msg_type(b),
                                    static_cast<char>(b[23])};

    case 'J':
      return LULDAuctionCollar{timestamp(b),
                               read_be32(b + 19),
                               read_be32(b + 23),
                               read_be32(b + 27),
                               read_be32(b + 31),
                               fixed_ascii<8>(b + 11),
                               locate(b), tracking(b), msg_type(b)};

    case 'h':
      return OperationalHalt{timestamp(b), fixed_ascii<8>(b + 11),
                             locate(b), tracking(b), msg_type(b),
                             static_cast<char>(b[19]),
                             static_cast<char>(b[20])};

    case 'A':
      return AddOrder{timestamp(b),
                      read_be64(b + 11),
                      read_be32(b + 20),
                      read_be32(b + 32),
                      fixed_ascii<8>(b + 24),
                      locate(b), tracking(b), msg_type(b),
                      static_cast<char>(b[19])};

    case 'F':
      return AddOrderWithMPID{timestamp(b),
                              read_be64(b + 11),
                              read_be32(b + 20),
                              read_be32(b + 32),
                              fixed_ascii<8>(b + 24),
                              fixed_ascii<4>(b + 36),
                              locate(b), tracking(b), msg_type(b),
                              static_cast<char>(b[19])};

    case 'E':
      return OrderExecuted{timestamp(b), read_be64(b + 11),
                           read_be64(b + 23), read_be32(b + 19),
                           locate(b), tracking(b), msg_type(b)};

    case 'C':
      return OrderExecutedWithPrice{timestamp(b),
                                    read_be64(b + 11),
                                    read_be64(b + 23),
                                    read_be32(b + 19),
                                    read_be32(b + 32),
                                    locate(b), tracking(b), msg_type(b),
                                    static_cast<char>(b[31])};

    case 'X':
      return OrderCancel{timestamp(b), read_be64(b + 11), read_be32(b + 19),
                         locate(b), tracking(b), msg_type(b)};

    case 'D':
      return OrderDelete{timestamp(b), read_be64(b + 11), locate(b),
                         tracking(b), msg_type(b)};

    case 'U':
      return OrderReplace{timestamp(b), read_be64(b + 11), read_be64(b + 19),
                          read_be32(b + 27), read_be32(b + 31), locate(b),
                          tracking(b), msg_type(b)};

    case 'P':
      return Trade{timestamp(b),
                   read_be64(b + 11),
                   read_be64(b + 36),
                   read_be32(b + 20),
                   read_be32(b + 32),
                   fixed_ascii<8>(b + 24),
                   locate(b), tracking(b), msg_type(b),
                   static_cast<char>(b[19])};

    case 'Q':
      return CrossTrade{timestamp(b),
                        read_be64(b + 11),
                        read_be64(b + 31),
                        read_be32(b + 27),
                        fixed_ascii<8>(b + 19),
                        locate(b), tracking(b), msg_type(b),
                        static_cast<char>(b[39])};

    case 'B':
      return BrokenTrade{timestamp(b), read_be64(b + 11), locate(b),
                         tracking(b), msg_type(b)};

    case 'I':
      return NOII{timestamp(b),
                  read_be64(b + 11),
                  read_be64(b + 19),
                  read_be32(b + 36),
                  read_be32(b + 40),
                  read_be32(b + 44),
                  fixed_ascii<8>(b + 28),
                  locate(b), tracking(b), msg_type(b),
                  static_cast<char>(b[27]),
                  static_cast<char>(b[48]),
                  static_cast<char>(b[49])};

    case 'N':
      return RetailInterest{timestamp(b), fixed_ascii<8>(b + 11),
                            locate(b), tracking(b), msg_type(b),
                            static_cast<char>(b[19])};

    case 'O':
      return DirectListingWithCapitalRaisePriceDiscovery{
          timestamp(b),       read_be64(b + 32),
          read_be32(b + 20),  read_be32(b + 24),
          read_be32(b + 28),  read_be32(b + 40),
          read_be32(b + 44),  fixed_ascii<8>(b + 11),
          locate(b),          tracking(b),
          msg_type(b),        static_cast<char>(b[19])};

    default:
      return UnknownMessage{offset, type, length, ParseErrorKind::UnknownType};
  }
}

Message parse_message_body(const MessageBodyView& view) {
  return parse_message_body(view.body, view.length, view.offset);
}

}  // namespace itch
