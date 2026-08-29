#include "book.hpp"

namespace ex {
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

} // namespace ex
