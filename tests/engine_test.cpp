#include "check.hpp"
#include "engine.hpp"

#include <cstring>
#include <random>
#include <vector>

using namespace ex;

namespace {

// Prices must be multiples of TickSize (100). 10000 == $1.00.
constexpr std::int64_t P0 = 10000;
constexpr std::int64_t P1 = 10100;

Command new_order(std::uint64_t seq, std::uint32_t client, std::uint32_t handle,
                  std::int64_t price, std::int64_t qty, Side side) {
    return Command{Seq{seq}, 0, ClientId{client}, ClientOrderId{handle},
                   Price{price}, Qty{qty}, CommandType::NewOrder, side, {}};
}

Command cancel(std::uint64_t seq, std::uint32_t client, std::uint32_t handle) {
    return Command{Seq{seq}, 0, ClientId{client}, ClientOrderId{handle},
                   Price{0}, Qty{0}, CommandType::Cancel, Side::Bid, {}};
}

// One command in, that command's events out. The engine appends rather than
// clearing, so a fresh vector per call is what isolates one command's output.
std::vector<Event> run(Engine& e, const Command& c) {
    std::vector<Event> out;
    e.apply(c, out);
    return out;
}

// --- validation --------------------------------------------------------------
// Order is OffTick -> BadQty -> DuplicateOrder: the two stateless checks, then
// the one that needs the book. Observable, so replay must reproduce it.

void rejects_off_tick_price() {
    Engine e;
    // 10050 is not a multiple of TickSize.
    const auto ev = run(e, new_order(1, 1, 1, 10050, 5, Side::Bid));
    CHECK(ev.size() == 1);
    CHECK(ev[0].type == EventType::Rejected);
    CHECK(ev[0].reason == RejectReason::OffTick);
    // A rejected order must leave no trace in the book.
    CHECK(!e.book().best_bid().has_value());
}

void rejects_negative_price() {
    Engine e;
    // On the grid, but negative -- valid_price requires raw(p) >= 0.
    const auto ev = run(e, new_order(1, 1, 1, -P0, 5, Side::Bid));
    CHECK(ev.size() == 1);
    CHECK(ev[0].reason == RejectReason::OffTick);
}

void accepts_zero_price() {          // decided 2026-08-31: zero is legal
    Engine e;
    const auto ev = run(e, new_order(1, 1, 1, 0, 5, Side::Bid));
    CHECK(ev.size() == 1);
    CHECK(ev[0].type == EventType::Accepted);
    CHECK(ev[0].reason == RejectReason::None);
    // It rests, at the very bottom of the book, where it will never cross.
    CHECK(e.book().best_bid().has_value());
    CHECK(raw(*e.book().best_bid()) == 0);
}

void rejects_zero_quantity() {
    Engine e;
    const auto ev = run(e, new_order(1, 1, 1, P0, 0, Side::Bid));
    CHECK(ev.size() == 1);
    CHECK(ev[0].reason == RejectReason::BadQty);
    CHECK(!e.book().best_bid().has_value());
}

void rejects_negative_quantity() {
    Engine e;
    const auto ev = run(e, new_order(1, 1, 1, P0, -5, Side::Bid));
    CHECK(ev.size() == 1);
    CHECK(ev[0].reason == RejectReason::BadQty);
}

void rejects_duplicate_client_order_id() {
    Engine e;
    run(e, new_order(1, 1, 7, P0, 5, Side::Bid));
    // Same client, same handle, while the first is still resting.
    const auto ev = run(e, new_order(2, 1, 7, P0, 5, Side::Bid));
    CHECK(ev.size() == 1);
    CHECK(ev[0].reason == RejectReason::DuplicateOrder);

    // A different client may reuse the same handle value: the key is the pair.
    const auto other = run(e, new_order(3, 2, 7, P0, 5, Side::Bid));
    CHECK(other[0].type == EventType::Accepted);
}

void duplicate_handle_is_free_again_after_cancel() {
    Engine e;
    run(e, new_order(1, 1, 7, P0, 5, Side::Bid));
    run(e, cancel(2, 1, 7));
    // The duplicate test is over *live* orders, not all-time uniqueness.
    const auto ev = run(e, new_order(3, 1, 7, P0, 5, Side::Bid));
    CHECK(ev.size() == 1);
    CHECK(ev[0].type == EventType::Accepted);
}

// --- resting -----------------------------------------------------------------

void resting_order_emits_accepted_with_remainder() {
    Engine e;
    const auto ev = run(e, new_order(1, 1, 1, P0, 5, Side::Bid));
    CHECK(ev.size() == 1);
    CHECK(ev[0].type == EventType::Accepted);
    // Nothing crossed, so the whole quantity rests.
    CHECK(raw(ev[0].qty) == 5);
    CHECK(raw(ev[0].price) == P0);
    CHECK(ev[0].side == Side::Bid);
}

void accepted_event_carries_the_commands_seq() {
    Engine e;
    const auto ev = run(e, new_order(42, 1, 1, P0, 5, Side::Bid));
    CHECK(ev[0].seq == Seq{42});
}

void order_id_equals_the_creating_commands_seq() {
    Engine e;
    const auto ev = run(e, new_order(42, 1, 1, P0, 5, Side::Bid));
    // No counter in engine state: the id *is* the sequence number.
    CHECK(ev[0].order == OrderId{42});
}

// --- matching ----------------------------------------------------------------

void non_crossing_order_just_rests() {
    Engine e;
    run(e, new_order(1, 1, 1, P1, 5, Side::Ask));   // ask 10100
    const auto ev = run(e, new_order(2, 2, 1, P0, 5, Side::Bid));  // bid 10000
    CHECK(ev.size() == 1);
    CHECK(ev[0].type == EventType::Accepted);
    CHECK(raw(ev[0].qty) == 5);
    CHECK(raw(*e.book().best_bid()) == P0);
    CHECK(raw(*e.book().best_ask()) == P1);
}

void crossing_order_fills_at_the_makers_price() {
    Engine e;
    run(e, new_order(1, 1, 1, P0, 5, Side::Ask));   // ask rests at 10000
    // Taker is willing to pay 10100, but the maker was there first at 10000.
    const auto ev = run(e, new_order(2, 2, 1, P1, 5, Side::Bid));
    CHECK(ev.size() == 2);
    CHECK(ev[0].type == EventType::Filled);
    CHECK(raw(ev[0].price) == P0);        // maker's price, not the taker's 10100
    CHECK(raw(ev[0].qty) == 5);
}

void partial_fill_rests_the_remainder() {
    Engine e;
    run(e, new_order(1, 1, 1, P0, 3, Side::Ask));
    const auto ev = run(e, new_order(2, 2, 1, P0, 5, Side::Bid));
    CHECK(ev.size() == 2);
    CHECK(ev[0].type == EventType::Filled);
    CHECK(raw(ev[0].qty) == 3);
    // Accepted comes last and carries what actually rested.
    CHECK(ev[1].type == EventType::Accepted);
    CHECK(raw(ev[1].qty) == 2);
    CHECK(raw(*e.book().best_bid()) == P0);
    CHECK(!e.book().best_ask().has_value());   // the ask side is drained
}

void full_fill_rests_nothing() {
    Engine e;
    run(e, new_order(1, 1, 1, P0, 5, Side::Ask));
    const auto ev = run(e, new_order(2, 2, 1, P0, 5, Side::Bid));
    CHECK(ev.size() == 2);
    CHECK(ev[0].type == EventType::Filled);
    CHECK(raw(ev[0].qty) == 5);
    // Accepted is still emitted -- every admitted order gets exactly one -- but
    // it reports a resting remainder of zero.
    CHECK(ev[1].type == EventType::Accepted);
    CHECK(raw(ev[1].qty) == 0);
    CHECK(!e.book().best_bid().has_value());
    CHECK(!e.book().best_ask().has_value());
}

void fill_walks_levels_best_price_first() {
    Engine e;
    run(e, new_order(1, 1, 1, P1, 2, Side::Ask));   // worse for the taker
    run(e, new_order(2, 2, 1, P0, 2, Side::Ask));   // better -- must fill first
    const auto ev = run(e, new_order(3, 3, 1, P1, 4, Side::Bid));
    CHECK(ev.size() == 3);
    CHECK(raw(ev[0].price) == P0);   // cheapest ask first, regardless of arrival
    CHECK(raw(ev[1].price) == P1);
    CHECK(ev[2].type == EventType::Accepted);
    CHECK(raw(ev[2].qty) == 0);
}

void fill_respects_fifo_within_one_level() {
    Engine e;
    run(e, new_order(1, 1, 1, P0, 2, Side::Ask));   // first in
    run(e, new_order(2, 2, 1, P0, 2, Side::Ask));   // second in, same price
    const auto ev = run(e, new_order(3, 3, 1, P0, 3, Side::Bid));
    CHECK(ev.size() == 3);
    // Same price, so only arrival order can break the tie.
    CHECK(ev[0].maker == OrderId{1});
    CHECK(raw(ev[0].qty) == 2);
    CHECK(ev[1].maker == OrderId{2});
    CHECK(raw(ev[1].qty) == 1);      // partially consumes the second
}

void taker_larger_than_the_whole_book_rests_what_is_left() {
    Engine e;
    run(e, new_order(1, 1, 1, P0, 2, Side::Ask));
    const auto ev = run(e, new_order(2, 2, 1, P0, 5, Side::Bid));
    CHECK(ev.size() == 2);
    CHECK(raw(ev[0].qty) == 2);      // took everything available
    CHECK(ev[1].type == EventType::Accepted);
    CHECK(raw(ev[1].qty) == 3);      // the rest rests
    CHECK(raw(*e.book().best_bid()) == P0);
}

void filled_event_names_both_maker_and_taker() {
    Engine e;
    run(e, new_order(1, 11, 77, P0, 5, Side::Ask));       // maker: client 11, handle 77
    const auto ev = run(e, new_order(2, 22, 88, P0, 5, Side::Bid));  // taker: 22 / 88
    CHECK(ev[0].type == EventType::Filled);
    // Taker on the primary fields...
    CHECK(ev[0].order == OrderId{2});
    CHECK(ev[0].client == ClientId{22});
    CHECK(ev[0].client_order == ClientOrderId{88});
    CHECK(ev[0].side == Side::Bid);                       // the aggressor's side
    // ...maker on its own, so M3 can route this fill to the maker's client
    // without the edge keeping an OrderId -> ClientId map of its own.
    CHECK(ev[0].maker == OrderId{1});
    CHECK(ev[0].maker_client == ClientId{11});
    CHECK(ev[0].maker_client_order == ClientOrderId{77});
}

// --- cancel ------------------------------------------------------------------

void cancel_removes_a_resting_order() {
    Engine e;
    run(e, new_order(1, 1, 7, P0, 5, Side::Bid));
    const auto ev = run(e, cancel(2, 1, 7));
    CHECK(ev.size() == 1);
    CHECK(ev[0].type == EventType::Cancelled);
    CHECK(ev[0].seq == Seq{2});          // the cancel command...
    CHECK(ev[0].order == OrderId{1});    // ...but the original order's id
    // Price, qty and side come from the resting order: a Cancel command leaves
    // them unset, and market data needs them to decrement the right level.
    CHECK(raw(ev[0].price) == P0);
    CHECK(raw(ev[0].qty) == 5);
    CHECK(ev[0].side == Side::Bid);
    CHECK(!e.book().best_bid().has_value());
}

void cancel_unknown_order_is_rejected() {
    Engine e;
    const auto ev = run(e, cancel(1, 1, 7));
    CHECK(ev.size() == 1);
    CHECK(ev[0].type == EventType::Rejected);
    CHECK(ev[0].reason == RejectReason::UnknownOrder);
}

void cancel_of_another_clients_order_is_rejected() {
    Engine e;
    run(e, new_order(1, 1, 7, P0, 5, Side::Bid));   // client 1 owns handle 7
    const auto ev = run(e, cancel(2, 2, 7));        // client 2 tries the same handle
    CHECK(ev.size() == 1);
    // UnknownOrder, deliberately -- a distinct "not yours" would confirm to
    // client 2 that someone else's order exists.
    CHECK(ev[0].reason == RejectReason::UnknownOrder);
    CHECK(e.book().best_bid().has_value());         // and it is untouched
}

// --- invariants ---------------------------------------------

constexpr std::uint32_t Seed = 20260906;
constexpr std::size_t   Len  = 4000;

// No std::uniform_int_distribution: it is not specified to yield the same values
// across standard library implementations, and the gate here is identical output
// across machines. std::mt19937 itself is specified.
std::vector<Command> session(std::uint32_t seed, std::size_t n) {
    std::mt19937 rng{seed};
    auto pick = [&rng](std::uint32_t m) { return static_cast<std::uint32_t>(rng() % m); };

    std::vector<Command> cmds;
    cmds.reserve(n);
    std::uint32_t next_handle = 1;

    for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t seq    = i + 1;
        const std::uint32_t client = 1 + pick(4);

        if (pick(5) == 0) {
            cmds.push_back(cancel(seq, client, 1 + pick(next_handle)));
            continue;
        }

        std::int64_t price = TickSize * static_cast<std::int64_t>(95 + pick(11));
        std::int64_t qty   = 1 + static_cast<std::int64_t>(pick(10));
        switch (pick(20)) {
        case 0: price += 1; break;   // off tick
        case 1: qty    = 0; break;   // bad qty
        default: break;
        }

        const Side side = pick(2) ? Side::Ask : Side::Bid;
        const std::uint32_t handle =
            pick(10) == 0 ? 1 + pick(next_handle) : next_handle++;
        cmds.push_back(new_order(seq, client, handle, price, qty, side));
    }
    return cmds;
}

std::int64_t resting_total(const Book& b) {
    std::vector<Book::Level> bid, ask;
    b.levels(Side::Bid, bid);
    b.levels(Side::Ask, ask);
    std::int64_t total = 0;
    for (const auto& l : bid) total += raw(l.qty);
    for (const auto& l : ask) total += raw(l.qty);
    return total;
}

void book_is_never_crossed_after_any_command() {
    Engine e;
    std::vector<Event> out;
    std::uint64_t first_bad = 0;
    int observed = 0;

    for (const Command& c : session(Seed, Len)) {
        e.apply(c, out);
        const auto bid = e.book().best_bid();
        const auto ask = e.book().best_ask();
        if (!bid || !ask) continue;
        ++observed;
        if (raw(*bid) >= raw(*ask) && first_bad == 0) first_bad = std::to_underlying(c.seq);
    }

    CHECK(first_bad == 0);
    CHECK(observed > 0);
}

void quantity_is_conserved_across_a_random_session() {
    Engine e;
    std::int64_t admitted = 0, traded = 0, cancelled = 0;
    int fills = 0, cancels = 0, rejects = 0;

    for (const Command& c : session(Seed, Len)) {
        std::vector<Event> ev;
        e.apply(c, ev);

        bool rejected = false;
        for (const Event& x : ev) {
            switch (x.type) {
            case EventType::Rejected:  rejected = true;        ++rejects; break;
            case EventType::Filled:    traded    += raw(x.qty); ++fills;   break;
            case EventType::Cancelled: cancelled += raw(x.qty); ++cancels; break;
            case EventType::Accepted:  break;
            }
        }
        if (!rejected && c.type == CommandType::NewOrder) admitted += raw(c.qty);
    }

    CHECK(resting_total(e.book()) == admitted - 2 * traded - cancelled);

    CHECK(fills   > 0);
    CHECK(cancels > 0);
    CHECK(rejects > 0);
}

void identical_command_sequences_produce_identical_events() {
    const std::vector<Command> cmds = session(Seed, Len);

    auto play = [&cmds](std::vector<Book::Level>& bid, std::vector<Book::Level>& ask) {
        Engine e;
        std::vector<Event> out;
        for (const Command& c : cmds) e.apply(c, out);
        e.book().levels(Side::Bid, bid);
        e.book().levels(Side::Ask, ask);
        return out;
    };

    std::vector<Book::Level> bid_a, ask_a, bid_b, ask_b;
    const std::vector<Event> a = play(bid_a, ask_a);
    const std::vector<Event> b = play(bid_b, ask_b);

    CHECK(!a.empty());
    CHECK(a.size() == b.size());
    if (a.size() == b.size()) {
        CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(Event)) == 0);
    }

    auto same_depth = [](const std::vector<Book::Level>& x,
                         const std::vector<Book::Level>& y) {
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i)
            if (raw(x[i].price) != raw(y[i].price) || raw(x[i].qty) != raw(y[i].qty))
                return false;
        return true;
    };

    CHECK(!bid_a.empty() || !ask_a.empty());
    CHECK(same_depth(bid_a, bid_b));
    CHECK(same_depth(ask_a, ask_b));
}

} // namespace

int main() {
    RUN(rejects_off_tick_price);
    RUN(rejects_negative_price);
    RUN(accepts_zero_price);
    RUN(rejects_zero_quantity);
    RUN(rejects_negative_quantity);
    RUN(rejects_duplicate_client_order_id);
    RUN(duplicate_handle_is_free_again_after_cancel);

    RUN(resting_order_emits_accepted_with_remainder);
    RUN(accepted_event_carries_the_commands_seq);
    RUN(order_id_equals_the_creating_commands_seq);

    RUN(non_crossing_order_just_rests);
    RUN(crossing_order_fills_at_the_makers_price);
    RUN(partial_fill_rests_the_remainder);
    RUN(full_fill_rests_nothing);
    RUN(fill_walks_levels_best_price_first);
    RUN(fill_respects_fifo_within_one_level);
    RUN(taker_larger_than_the_whole_book_rests_what_is_left);
    RUN(filled_event_names_both_maker_and_taker);

    RUN(cancel_removes_a_resting_order);
    RUN(cancel_unknown_order_is_rejected);
    RUN(cancel_of_another_clients_order_is_rejected);

    RUN(book_is_never_crossed_after_any_command);
    RUN(quantity_is_conserved_across_a_random_session);
    RUN(identical_command_sequences_produce_identical_events);

    return test::summary();
}
