// Normalised book events.
//
// Every feed decoder (LOBSTER CSV, Nasdaq ITCH, Databento MBO, the synthetic
// generator) produces this one type, so the book never learns which venue it is
// reading. Adding a venue is a decoder, not a book change.
#pragma once

#include <cstdint>
#include <string_view>

#include "lob/core/types.hpp"

namespace lob {

enum class EventType : std::uint8_t {
  Add = 0,   // new resting limit order joins the back of its price level's queue
  Reduce,    // partial cancel: qty shrinks, queue position is KEPT
  Delete,    // full cancel: order leaves the book
  Execute,   // a resting order is filled, wholly or partly, from the front
  Replace,   // cancel + add under a new id. Priority is LOST — that is the point
  Clear,     // wipe the book (session boundary, halt resume)
  // An aggressive order arriving. NOT a book mutation: it must go through the
  // matching engine, which decides who it trades with. OrderBook::apply ignores
  // it deliberately — a book that consumes a real exchange feed never sees one,
  // because the exchange already matched it before publishing.
  Aggress,
  Count
};

[[nodiscard]] constexpr std::string_view event_name(EventType t) noexcept {
  switch (t) {
    case EventType::Add:     return "add";
    case EventType::Reduce:  return "reduce";
    case EventType::Delete:  return "delete";
    case EventType::Execute: return "execute";
    case EventType::Replace: return "replace";
    case EventType::Clear:   return "clear";
    case EventType::Aggress: return "aggress";
    case EventType::Count:   return "?";
  }
  return "?";
}

// Fixed-size POD so it can go straight into a ring buffer or a journal.
struct BookEvent {
  Nanos       ts        = 0;        // exchange timestamp
  SeqNum      seq       = 0;        // feed sequence number
  OrderId     order_id  = 0;
  OrderId     new_id    = 0;        // Replace only
  Ticks       price     = 0;        // Add / Replace only
  Qty         qty       = 0;        // Add: size. Reduce/Execute: amount removed
  EventType   type      = EventType::Add;
  Side        side      = Side::Bid;
  std::uint8_t _pad[6]  = {};
};
static_assert(sizeof(BookEvent) == 56);
static_assert(std::is_trivially_copyable_v<BookEvent>);

// Why an operation failed. Returned rather than thrown: the hot path must not
// unwind, and a feed with a gap in it will produce these routinely.
enum class BookError : std::uint8_t {
  Ok = 0,
  UnknownOrder,      // reduce/delete/execute for an id we never saw (feed gap)
  DuplicateOrder,    // add for an id already resting
  PriceOutOfWindow,  // price outside the book's tick window
  PoolExhausted,     // no free order slots
  BadQuantity,       // non-positive size, or reducing by more than remains
  Count
};

[[nodiscard]] constexpr std::string_view error_name(BookError e) noexcept {
  switch (e) {
    case BookError::Ok:               return "ok";
    case BookError::UnknownOrder:     return "unknown_order";
    case BookError::DuplicateOrder:   return "duplicate_order";
    case BookError::PriceOutOfWindow: return "price_out_of_window";
    case BookError::PoolExhausted:    return "pool_exhausted";
    case BookError::BadQuantity:      return "bad_quantity";
    case BookError::Count:            return "?";
  }
  return "?";
}

}  // namespace lob
