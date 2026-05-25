#pragma once

#include "types.hpp"

#include <array>
#include <string_view>

namespace itch {

constexpr u16 kUnknownLength = 0;

namespace detail {

inline constexpr std::array<u8, 256> kExpectedLengths = [] {
  std::array<u8, 256> lengths{};
  lengths[static_cast<unsigned char>('S')] = 12;
  lengths[static_cast<unsigned char>('R')] = 39;
  lengths[static_cast<unsigned char>('H')] = 25;
  lengths[static_cast<unsigned char>('Y')] = 20;
  lengths[static_cast<unsigned char>('L')] = 26;
  lengths[static_cast<unsigned char>('V')] = 35;
  lengths[static_cast<unsigned char>('W')] = 12;
  lengths[static_cast<unsigned char>('K')] = 28;
  lengths[static_cast<unsigned char>('J')] = 35;
  lengths[static_cast<unsigned char>('h')] = 21;
  lengths[static_cast<unsigned char>('A')] = 36;
  lengths[static_cast<unsigned char>('F')] = 40;
  lengths[static_cast<unsigned char>('E')] = 31;
  lengths[static_cast<unsigned char>('C')] = 36;
  lengths[static_cast<unsigned char>('X')] = 23;
  lengths[static_cast<unsigned char>('D')] = 19;
  lengths[static_cast<unsigned char>('U')] = 35;
  lengths[static_cast<unsigned char>('P')] = 44;
  lengths[static_cast<unsigned char>('Q')] = 40;
  lengths[static_cast<unsigned char>('B')] = 19;
  lengths[static_cast<unsigned char>('I')] = 50;
  lengths[static_cast<unsigned char>('N')] = 20;
  lengths[static_cast<unsigned char>('O')] = 48;
  return lengths;
}();

}  // namespace detail

inline u16 expected_length(char type) {
  return detail::kExpectedLengths[static_cast<unsigned char>(type)];
}

inline std::string_view message_name(char type) {
  switch (type) {
    case 'S': return "SystemEvent";
    case 'R': return "StockDirectory";
    case 'H': return "StockTradingAction";
    case 'Y': return "RegSHORestriction";
    case 'L': return "MarketParticipantPosition";
    case 'V': return "MWCBDeclineLevel";
    case 'W': return "MWCBStatus";
    case 'K': return "IPOQuotingPeriodUpdate";
    case 'J': return "LULDAuctionCollar";
    case 'h': return "OperationalHalt";
    case 'A': return "AddOrder";
    case 'F': return "AddOrderWithMPID";
    case 'E': return "OrderExecuted";
    case 'C': return "OrderExecutedWithPrice";
    case 'X': return "OrderCancel";
    case 'D': return "OrderDelete";
    case 'U': return "OrderReplace";
    case 'P': return "Trade";
    case 'Q': return "CrossTrade";
    case 'B': return "BrokenTrade";
    case 'I': return "NOII";
    case 'N': return "RetailInterest";
    case 'O': return "DirectListingWithCapitalRaisePriceDiscovery";
    default: return "Unknown";
  }
}

inline std::string_view message_tag(char type) {
  switch (type) {
    case 'S': return "SYS";
    case 'R': return "DIR";
    case 'H': return "ACT";
    case 'Y': return "REG";
    case 'L': return "POS";
    case 'V': return "DCL";
    case 'W': return "STS";
    case 'K': return "IPO";
    case 'J': return "COL";
    case 'h': return "HLT";
    case 'A': return "ADD";
    case 'F': return "ADM";
    case 'E': return "EXC";
    case 'C': return "EXP";
    case 'X': return "CNL";
    case 'D': return "DEL";
    case 'U': return "RPL";
    case 'P': return "TRD";
    case 'Q': return "CRX";
    case 'B': return "BRK";
    case 'I': return "NOI";
    case 'N': return "RTL";
    case 'O': return "DSC";
    default: return "UNK";
  }
}

}  // namespace itch
