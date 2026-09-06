#include "engine.hpp"
#include <algorithm>
#include <utility>

namespace ex {
namespace {


// Every event the engine emits is built by this helper or inline
// Every field is written explicitly
Event reject(const Command& cmd, RejectReason reason) {
    return Event{
        .seq   = cmd.seq,
        // Even a rejected order gets the OrderId it would have had
        .order = OrderId{std::to_underlying(cmd.seq)},
        .maker = OrderId{},
        // Echoed back so the edge can route the reject
        .client             = cmd.client,
        .client_order       = cmd.client_order,
        .price              = cmd.price,
        .qty                = cmd.qty,
        .maker_client       = ClientId{},
        .maker_client_order = ClientOrderId{},
        .type               = EventType::Rejected,
        .side               = cmd.side,
        .reason             = reason,
        ._pad               = {},
    };
}

} // namespace

void Engine::apply(const Command& cmd, std::vector<Event>& out) {
    // Appended to, never cleared, so one vector can hold a whole session.
    switch (cmd.type) {
    case CommandType::NewOrder: new_order(cmd, out); break;
    case CommandType::Cancel:   cancel(cmd, out);    break;

    // The journal is replayed from disk and a torn byte can produce a type which is neither value
    default:
        out.push_back(reject(cmd, RejectReason::BadCommand));
        break;
    }
}

void Engine::new_order(const Command& cmd, std::vector<Event>& out) {
    // Fixed order: the two stateless checks, then the one that needs the book.
    // A command that is bother off-tick and duplicate returns OffTick


    // Off the tick grid or negative price
    if (!valid_price(cmd.price)) {
        out.push_back(reject(cmd, RejectReason::OffTick));
        return;
    }

    // Zero or negative qty
    if (raw(cmd.qty) <= 0) {
        out.push_back(reject(cmd, RejectReason::BadQty));
        return;
    }

    // Check for duplicate order
    if (book_.contains(cmd.client, cmd.client_order)) {
        out.push_back(reject(cmd, RejectReason::DuplicateOrder));
        return;
    }

    // The OrderId is the Seq of the command that created it - always unique
    const OrderId taker{std::to_underlying(cmd.seq)};
    Qty remaining = cmd.qty;


    // Matching logic:
    // Every iteration will either break of subtract a fill from remaining
    // No self-trade check
    while (raw(remaining) > 0) {
        // Best price on the opposite side
        const std::optional<Price> best =
            cmd.side == Side::Bid ? book_.best_ask() : book_.best_bid();

        if (!best) break;                                 // nothing left to trade against
        if (!crosses(cmd.side, cmd.price, *best)) break;   // best resting price is not good enough

        const RestingOrder* maker = book_.front(opposite(cmd.side), *best);


        // Every field in the Filled event is copied here
        const OrderId       maker_id     = maker->id;
        const ClientId      maker_client = maker->client;
        const ClientOrderId maker_handle = maker->client_order;
        const Qty           fill         = std::min(remaining, maker->remaining);

        book_.reduce_front(opposite(cmd.side), *best, fill);
        remaining -= fill;

        out.push_back(Event{
            .seq   = cmd.seq,
            .order = taker,
            .maker = maker_id,
            .client             = cmd.client,
            .client_order       = cmd.client_order,
            // The trade prints at the maker's price
            .price              = *best,
            .qty                = fill,
            .maker_client       = maker_client,
            .maker_client_order = maker_handle,
            .type               = EventType::Filled,
            // The aggressor's side
            .side               = cmd.side,
            .reason             = RejectReason::None,
            ._pad               = {},
        });
    }

    // Never rests a 0 remainder
    if (raw(remaining) > 0) {
        book_.rest(RestingOrder{
            taker, cmd.client, cmd.client_order, cmd.price, remaining, cmd.side});
    }


    // Accepted is emitted for every admittted order
    out.push_back(Event{
        .seq   = cmd.seq,
        .order = taker,
        .maker = OrderId{},
        .client             = cmd.client,
        .client_order       = cmd.client_order,
        .price              = cmd.price,
        .qty                = remaining,
        .maker_client       = ClientId{},
        .maker_client_order = ClientOrderId{},
        .type               = EventType::Accepted,
        .side               = cmd.side,
        .reason             = RejectReason::None,
        ._pad               = {},
    });
}

void Engine::cancel(const Command& cmd, std::vector<Event>& out) {

    // Gets order and removes if exists
    const std::optional<RestingOrder> gone = book_.remove(cmd.client, cmd.client_order);
    // If it doesnt exist or the order doesnt belond to them return UnknownOrder
    if (!gone) {
        out.push_back(reject(cmd, RejectReason::UnknownOrder));
        return;
    }

    out.push_back(Event{
        .seq   = cmd.seq,
        // The id from when the order was created
        .order = gone->id,
        .maker = OrderId{},
        .client             = cmd.client,
        .client_order       = cmd.client_order,
        // Taken from the resting order
        .price              = gone->price,
        .qty                = gone->remaining,
        .maker_client       = ClientId{},
        .maker_client_order = ClientOrderId{},
        .type               = EventType::Cancelled,
        .side               = gone->side,
        .reason             = RejectReason::None,
        ._pad               = {},
    });
}

} // namespace ex
