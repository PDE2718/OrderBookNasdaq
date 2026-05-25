#pragma once

#include "fixed_ascii.hpp"
#include "types.hpp"

#include <type_traits>
#include <variant>

namespace itch {

enum class ParseErrorKind {
  EmptyBody,
  UnknownType,
  LengthMismatch,
  ShortBody,
};

struct SystemEvent {
  u64 timestamp = 0;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char event_code = '\0';
};

struct StockDirectory {
  u64 timestamp = 0;
  u32 round_lot_size = 0;
  u32 etp_leverage_factor = 0;
  Stock stock;
  IssueSubType issue_sub_type;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char market_category = '\0';
  char financial_status_indicator = '\0';
  char round_lots_only = '\0';
  char issue_classification = '\0';
  char authenticity = '\0';
  char short_sale_threshold_indicator = '\0';
  char ipo_flag = '\0';
  char luld_reference_price_tier = '\0';
  char etp_flag = '\0';
  char inverse_indicator = '\0';
};

struct StockTradingAction {
  u64 timestamp = 0;
  Stock stock;
  Reason reason;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char trading_state = '\0';
  char reserved = '\0';
};

struct RegSHORestriction {
  u64 timestamp = 0;
  Stock stock;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char reg_sho_action = '\0';
};

struct MarketParticipantPosition {
  u64 timestamp = 0;
  Stock stock;
  MPID mpid;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char primary_market_maker = '\0';
  char market_maker_mode = '\0';
  char market_participant_state = '\0';
};

struct MWCBDeclineLevel {
  u64 timestamp = 0;
  u64 level1 = 0;
  u64 level2 = 0;
  u64 level3 = 0;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
};

struct MWCBStatus {
  u64 timestamp = 0;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char breached_level = '\0';
};

struct IPOQuotingPeriodUpdate {
  u64 timestamp = 0;
  u32 ipo_quotation_release_time = 0;
  u32 ipo_price = 0;
  Stock stock;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char ipo_quotation_release_qualifier = '\0';
};

struct LULDAuctionCollar {
  u64 timestamp = 0;
  u32 auction_collar_reference_price = 0;
  u32 upper_auction_collar_price = 0;
  u32 lower_auction_collar_price = 0;
  u32 auction_collar_extension = 0;
  Stock stock;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
};

struct OperationalHalt {
  u64 timestamp = 0;
  Stock stock;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char market_code = '\0';
  char operational_halt_action = '\0';
};

struct AddOrder {
  u64 timestamp = 0;
  u64 order_reference_number = 0;
  u32 shares = 0;
  u32 price = 0;
  Stock stock;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char buy_sell_indicator = '\0';
};

struct AddOrderWithMPID {
  u64 timestamp = 0;
  u64 order_reference_number = 0;
  u32 shares = 0;
  u32 price = 0;
  Stock stock;
  Attribution attribution;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char buy_sell_indicator = '\0';
};

struct OrderExecuted {
  u64 timestamp = 0;
  u64 order_reference_number = 0;
  u64 match_number = 0;
  u32 executed_shares = 0;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
};

struct OrderExecutedWithPrice {
  u64 timestamp = 0;
  u64 order_reference_number = 0;
  u64 match_number = 0;
  u32 executed_shares = 0;
  u32 execution_price = 0;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char printable = '\0';
};

struct OrderCancel {
  u64 timestamp = 0;
  u64 order_reference_number = 0;
  u32 cancelled_shares = 0;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
};

struct OrderDelete {
  u64 timestamp = 0;
  u64 order_reference_number = 0;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
};

struct OrderReplace {
  u64 timestamp = 0;
  u64 original_order_reference_number = 0;
  u64 new_order_reference_number = 0;
  u32 shares = 0;
  u32 price = 0;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
};

struct Trade {
  u64 timestamp = 0;
  u64 order_reference_number = 0;
  u64 match_number = 0;
  u32 shares = 0;
  u32 price = 0;
  Stock stock;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char buy_sell_indicator = '\0';
};

struct CrossTrade {
  u64 timestamp = 0;
  u64 shares = 0;
  u64 match_number = 0;
  u32 cross_price = 0;
  Stock stock;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char cross_type = '\0';
};

struct BrokenTrade {
  u64 timestamp = 0;
  u64 match_number = 0;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
};

struct NOII {
  u64 timestamp = 0;
  u64 paired_shares = 0;
  u64 imbalance_shares = 0;
  u32 far_price = 0;
  u32 near_price = 0;
  u32 current_reference_price = 0;
  Stock stock;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char imbalance_direction = '\0';
  char cross_type = '\0';
  char price_variation_indicator = '\0';
};

struct RetailInterest {
  u64 timestamp = 0;
  Stock stock;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char interest_flag = '\0';
};

struct DirectListingWithCapitalRaisePriceDiscovery {
  u64 timestamp = 0;
  u64 near_execution_time = 0;
  u32 minimum_allowable_price = 0;
  u32 maximum_allowable_price = 0;
  u32 near_execution_price = 0;
  u32 lower_price_range_collar = 0;
  u32 upper_price_range_collar = 0;
  Stock stock;
  u16 stock_locate = 0;
  u16 tracking_number = 0;
  char type = '\0';
  char open_eligibility_status = '\0';
};

struct UnknownMessage {
  u64 offset = 0;
  char type = '\0';
  u16 length = 0;
  ParseErrorKind reason = ParseErrorKind::UnknownType;
};

struct MalformedMessage {
  u64 offset = 0;
  char type = '\0';
  u16 length = 0;
  u16 expected_length = 0;
  ParseErrorKind reason = ParseErrorKind::LengthMismatch;
};

using Message = std::variant<
    SystemEvent,
    StockDirectory,
    StockTradingAction,
    RegSHORestriction,
    MarketParticipantPosition,
    MWCBDeclineLevel,
    MWCBStatus,
    IPOQuotingPeriodUpdate,
    LULDAuctionCollar,
    OperationalHalt,
    AddOrder,
    AddOrderWithMPID,
    OrderExecuted,
    OrderExecutedWithPrice,
    OrderCancel,
    OrderDelete,
    OrderReplace,
    Trade,
    CrossTrade,
    BrokenTrade,
    NOII,
    RetailInterest,
    DirectListingWithCapitalRaisePriceDiscovery,
    UnknownMessage,
    MalformedMessage>;

using BookMessage = std::variant<
    AddOrder,
    AddOrderWithMPID,
    OrderExecuted,
    OrderExecutedWithPrice,
    OrderCancel,
    OrderDelete,
    OrderReplace>;

template <typename T>
inline constexpr bool is_book_message_v =
    std::is_same_v<std::remove_cvref_t<T>, AddOrder> ||
    std::is_same_v<std::remove_cvref_t<T>, AddOrderWithMPID> ||
    std::is_same_v<std::remove_cvref_t<T>, OrderExecuted> ||
    std::is_same_v<std::remove_cvref_t<T>, OrderExecutedWithPrice> ||
    std::is_same_v<std::remove_cvref_t<T>, OrderCancel> ||
    std::is_same_v<std::remove_cvref_t<T>, OrderDelete> ||
    std::is_same_v<std::remove_cvref_t<T>, OrderReplace>;

}  // namespace itch
