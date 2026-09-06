#pragma once
#include "types.hpp"
#include <type_traits>

namespace ex {

enum class CommandType : std::uint8_t { NewOrder = 1, Cancel = 2 };

// The engine's sequenced_input
struct Command {
    Seq            seq;
    std::uint64_t  recv_ts;      // nanoseconds, edge-stamped
    ClientId       client;
    ClientOrderId  client_order; // NewOrder: the new handle. Cancel: the target.
    Price          price;        // NewOrder only
    Qty            qty;          // NewOrder only
    CommandType    type;
    Side           side;         // NewOrder only
    std::uint8_t   _pad[6];      // explicit, so the struct has no implicit holes
};

static_assert(sizeof(Command) == 48);
static_assert(alignof(Command) == 8);
static_assert(std::has_unique_object_representations_v<Command>);

} // namespace ex
