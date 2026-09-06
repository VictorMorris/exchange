#pragma once
#include "types.hpp"
#include <type_traits>

namespace ex {

enum class EventType : std::uint8_t {
    Accepted = 1,   // order rested (fully or partially)
    Rejected = 2,
    Filled   = 3,   // one maker/taker match; emitted per fill, not per order
    Cancelled = 4,
};

enum class RejectReason : std::uint8_t {
    None = 0,
    OffTick = 1,
    BadQty = 2,
    UnknownOrder = 3,     // cancel for an id the book does not hold
    DuplicateOrder = 4,
    BadCommand = 5,
};

// One stream. Order entry routes by client; market data derives L2 deltas from Filled/Accepted/Cancelled.
struct Event {
    Seq           seq;          // the Command that produced this event
    OrderId       order;        // taker for Filled, subject otherwise
    OrderId       maker;        // Filled only
    ClientId      client;
    ClientOrderId client_order;
    Price         price;        // Filled: the maker's price
    Qty           qty;          // Filled: fill size. Accepted: resting remainder.
    ClientId      maker_client;
    ClientOrderId maker_client_order;
    EventType     type;
    Side          side;
    RejectReason  reason;
    std::uint8_t _pad[5];
};

static_assert(sizeof(Event) == 64);
static_assert(alignof(Event) == 8);
static_assert(std::has_unique_object_representations_v<Event>);

} // namespace ex
