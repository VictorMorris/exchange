#include "book.hpp"

namespace ex {

// Descending, highest bid
// Empty levels must have been dropped or this will return a level with no orders
std::optional<Price> Book::best_bid() const noexcept {
    if(!bids.empty()) return bids.begin()->first;
    return std::nullopt;
}

// Ascending, lowest ask
// Empty levels must have been dropped or this will return a level with no orders
std::optional<Price> Book::best_ask() const noexcept {
    if(!asks.empty()) return asks.begin()->first;
    return std::nullopt;
}
const RestingOrder* Book::front(Side side, Price price) const noexcept {
    if(side == Side::Ask) return front_from(asks, price);
    else                  return front_from(bids, price);
}

void Book::rest(const RestingOrder& order) {
    if (order.side == Side::Bid) rest_in(bids, order);
    else                         rest_in(asks, order);
}

std::optional<RestingOrder> Book::remove(ClientId client, ClientOrderId handle) noexcept {
    const std::uint64_t key = handle_key(client, handle);

    auto it = by_handle.find(key); // key, location
    if (it == by_handle.end()) return std::nullopt;

    const Location loc = it->second;
    by_handle.erase(it); // Erase from index

    RestingOrder value = *loc.node;

    if (loc.side == Side::Ask) erase_from(asks, loc);
    else                       erase_from(bids, loc);

    return value;
}

// Reduces from makers side
void Book::reduce_front(Side side, Price price, Qty qty) noexcept {
    if(side == Side::Bid) reduce_from(bids, price, qty);
    else                  reduce_from(asks, price, qty);
}

} // namespace ex
