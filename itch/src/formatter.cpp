#include "formatter.hpp"

#include "message_types.hpp"

#include <iomanip>
#include <sstream>
#include <type_traits>

namespace itch {
namespace {

std::string printable(char c) {
  if (c == '\0') {
    return "\\0";
  }
  if (c == ' ') {
    return "<space>";
  }
  return std::string(1, c);
}

std::string_view reason_text(ParseErrorKind reason) {
  switch (reason) {
    case ParseErrorKind::EmptyBody:
      return "empty message body";
    case ParseErrorKind::UnknownType:
      return "unknown ITCH message type";
    case ParseErrorKind::LengthMismatch:
      return "message length mismatch";
    case ParseErrorKind::ShortBody:
      return "message body shorter than length";
  }
  return "unknown parse error";
}

template <typename T>
void append_common(std::ostream& os, const T& m) {
  os << message_tag(m.type) << " " << message_name(m.type)
     << " type=" << printable(m.type)
     << " locate=" << m.stock_locate
     << " tracking=" << m.tracking_number
     << " ts=" << m.timestamp;
}

template <typename T>
void append_message(std::ostream& os, const T& m) {
  if constexpr (std::is_same_v<T, UnknownMessage>) {
    os << "UNK Unknown"
       << " type=" << printable(m.type)
       << " offset=" << m.offset
       << " len=" << m.length
       << " reason=\"" << reason_text(m.reason) << "\"";
  } else if constexpr (std::is_same_v<T, MalformedMessage>) {
    os << "BAD Malformed"
       << " type=" << printable(m.type)
       << " offset=" << m.offset
       << " len=" << m.length
       << " expected=" << m.expected_length
       << " reason=\"" << reason_text(m.reason) << "\"";
  } else {
    append_common(os, m);
  }

  if constexpr (std::is_same_v<T, SystemEvent>) {
    os << " event=" << printable(m.event_code);
  } else if constexpr (std::is_same_v<T, StockDirectory>) {
    os << " stock=" << m.stock.trimmed()
       << " market=" << printable(m.market_category)
       << " fin=" << printable(m.financial_status_indicator)
       << " roundLot=" << m.round_lot_size
       << " roundLotsOnly=" << printable(m.round_lots_only)
       << " issueClass=" << printable(m.issue_classification)
       << " issueSubType=" << m.issue_sub_type.trimmed()
       << " auth=" << printable(m.authenticity)
       << " shortSale=" << printable(m.short_sale_threshold_indicator)
       << " ipo=" << printable(m.ipo_flag)
       << " luldTier=" << printable(m.luld_reference_price_tier)
       << " etp=" << printable(m.etp_flag)
       << " etpLev=" << m.etp_leverage_factor
       << " inverse=" << printable(m.inverse_indicator);
  } else if constexpr (std::is_same_v<T, StockTradingAction>) {
    os << " stock=" << m.stock.trimmed()
       << " tradingState=" << printable(m.trading_state)
       << " reserved=" << printable(m.reserved)
       << " reason=" << m.reason.trimmed();
  } else if constexpr (std::is_same_v<T, RegSHORestriction>) {
    os << " stock=" << m.stock.trimmed() << " action=" << printable(m.reg_sho_action);
  } else if constexpr (std::is_same_v<T, MarketParticipantPosition>) {
    os << " mpid=" << m.mpid.trimmed()
       << " stock=" << m.stock.trimmed()
       << " primaryMM=" << printable(m.primary_market_maker)
       << " mmMode=" << printable(m.market_maker_mode)
       << " participantState=" << printable(m.market_participant_state);
  } else if constexpr (std::is_same_v<T, MWCBDeclineLevel>) {
    os << " level1=" << m.level1
       << " level2=" << m.level2
       << " level3=" << m.level3;
  } else if constexpr (std::is_same_v<T, MWCBStatus>) {
    os << " breachedLevel=" << printable(m.breached_level);
  } else if constexpr (std::is_same_v<T, IPOQuotingPeriodUpdate>) {
    os << " stock=" << m.stock.trimmed()
       << " releaseTime=" << m.ipo_quotation_release_time
       << " qualifier=" << printable(m.ipo_quotation_release_qualifier)
       << " ipoPrice=" << m.ipo_price;
  } else if constexpr (std::is_same_v<T, LULDAuctionCollar>) {
    os << " stock=" << m.stock.trimmed()
       << " refPrice=" << m.auction_collar_reference_price
       << " upper=" << m.upper_auction_collar_price
       << " lower=" << m.lower_auction_collar_price
       << " extension=" << m.auction_collar_extension;
  } else if constexpr (std::is_same_v<T, OperationalHalt>) {
    os << " stock=" << m.stock.trimmed()
       << " marketCode=" << printable(m.market_code)
       << " action=" << printable(m.operational_halt_action);
  } else if constexpr (std::is_same_v<T, AddOrder>) {
    os << " ref=" << m.order_reference_number
       << " side=" << printable(m.buy_sell_indicator)
       << " shares=" << m.shares
       << " stock=" << m.stock.trimmed()
       << " price=" << m.price;
  } else if constexpr (std::is_same_v<T, AddOrderWithMPID>) {
    os << " ref=" << m.order_reference_number
       << " side=" << printable(m.buy_sell_indicator)
       << " shares=" << m.shares
       << " stock=" << m.stock.trimmed()
       << " price=" << m.price
       << " attribution=" << m.attribution.trimmed();
  } else if constexpr (std::is_same_v<T, OrderExecuted>) {
    os << " ref=" << m.order_reference_number
       << " executedShares=" << m.executed_shares
       << " match=" << m.match_number;
  } else if constexpr (std::is_same_v<T, OrderExecutedWithPrice>) {
    os << " ref=" << m.order_reference_number
       << " executedShares=" << m.executed_shares
       << " match=" << m.match_number
       << " printable=" << printable(m.printable)
       << " executionPrice=" << m.execution_price;
  } else if constexpr (std::is_same_v<T, OrderCancel>) {
    os << " ref=" << m.order_reference_number
       << " cancelledShares=" << m.cancelled_shares;
  } else if constexpr (std::is_same_v<T, OrderDelete>) {
    os << " ref=" << m.order_reference_number;
  } else if constexpr (std::is_same_v<T, OrderReplace>) {
    os << " origRef=" << m.original_order_reference_number
       << " newRef=" << m.new_order_reference_number
       << " shares=" << m.shares
       << " price=" << m.price;
  } else if constexpr (std::is_same_v<T, Trade>) {
    os << " ref=" << m.order_reference_number
       << " side=" << printable(m.buy_sell_indicator)
       << " shares=" << m.shares
       << " stock=" << m.stock.trimmed()
       << " price=" << m.price
       << " match=" << m.match_number;
  } else if constexpr (std::is_same_v<T, CrossTrade>) {
    os << " shares=" << m.shares
       << " stock=" << m.stock.trimmed()
       << " crossPrice=" << m.cross_price
       << " match=" << m.match_number
       << " crossType=" << printable(m.cross_type);
  } else if constexpr (std::is_same_v<T, BrokenTrade>) {
    os << " match=" << m.match_number;
  } else if constexpr (std::is_same_v<T, NOII>) {
    os << " pairedShares=" << m.paired_shares
       << " imbalanceShares=" << m.imbalance_shares
       << " direction=" << printable(m.imbalance_direction)
       << " stock=" << m.stock.trimmed()
       << " farPrice=" << m.far_price
       << " nearPrice=" << m.near_price
       << " currentRefPrice=" << m.current_reference_price
       << " crossType=" << printable(m.cross_type)
       << " priceVariation=" << printable(m.price_variation_indicator);
  } else if constexpr (std::is_same_v<T, RetailInterest>) {
    os << " stock=" << m.stock.trimmed() << " interest=" << printable(m.interest_flag);
  } else if constexpr (std::is_same_v<T, DirectListingWithCapitalRaisePriceDiscovery>) {
    os << " stock=" << m.stock.trimmed()
       << " eligibility=" << printable(m.open_eligibility_status)
       << " minPrice=" << m.minimum_allowable_price
       << " maxPrice=" << m.maximum_allowable_price
       << " nearPrice=" << m.near_execution_price
       << " nearTime=" << m.near_execution_time
       << " lowerCollar=" << m.lower_price_range_collar
       << " upperCollar=" << m.upper_price_range_collar;
  }
}

}  // namespace

std::string format_message(const MessageBodyView& view, const Message& message, u64 index) {
  std::ostringstream os;
  os << "#" << index << " offset=" << view.offset << " len=" << view.length << " ";
  std::visit([&](const auto& m) { append_message(os, m); }, message);
  return os.str();
}

}  // namespace itch
