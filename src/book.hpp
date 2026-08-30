#pragma once
#include "types.hpp"
#include <cstddef>
#include <optional>
#include <vector>
#include <map>
#include <list>
#include <unordered_map>
#include <cassert>

namespace ex {

struct RestingOrder {
    OrderId       id;
    ClientId      client;
    ClientOrderId client_order;
    Price         price;
    Qty           remaining;
    Side          side;
};



class Book {
public:
    // Highest bid / lowest ask, empty if that side has no resting quantity.
    std::optional<Price> best_bid() const noexcept;
    std::optional<Price> best_ask() const noexcept;

    // First order at price on side
    RestingOrder* front(Side side, Price price) noexcept;

    // Append to the back of the level's queue, creating the level if needed.
    void rest(const RestingOrder& order);

    // Reduce the front order at price by qty, pop when reaches zero
    void reduce_front(Side side, Price price, Qty qty) noexcept;

    // Remove by client handle. Returns the removed order, empty if not found.
    std::optional<RestingOrder> remove(ClientId client, ClientOrderId handle) noexcept;

    // Aggregated depth for market data
    struct Level { Price price; Qty qty; };
    void levels(Side side, std::vector<Level>& out) const;

private:
    using Queue = std::list<RestingOrder>;
    std::map<Price, Queue, std::greater<Price>> bids;
    std::map<Price, Queue, std::less<Price>> asks;

    struct Location {
        Side            side;
        Price           price; // Looking up price is O(logn)
        Queue::iterator node; // List iterator pointing at the order itself
    };

    std::unordered_map<std::uint64_t, Location> by_handle;

    template <class M>
    void rest_in(M& book, const RestingOrder& order);

    // Erase one order from book, remove level if empty
    template <class M>
    void erase_from(M& book, const Location& loc) noexcept;

    template <class M>
    void reduce_from(M& book, Price price, Qty qty) noexcept;
};



template <class M>
void Book::rest_in(M& book, const RestingOrder& order) {
    // Finds existing level or creates empty queue (maps default constructs the value if the key is absent)
    Queue& q = book[order.price]; 
    // Inserts at end (Insert returns an iterator to the element it just placed)
    auto node = q.insert(q.end(), order); 
    // Builds index entry in place
    // Engine must reject duplicate (clientId, clientOrderId)
    by_handle.emplace(handle_key(order.client, order.client_order),
                        Location{order.side, order.price, node});
}

template <class M>
void Book::erase_from(M& book, const Location& loc) noexcept {
    auto level = book.find(loc.price); // price, queue
    level->second.erase(loc.node); // erase order from queue
    if (level->second.empty()) book.erase(level); // Remove empty levels
}

template <class M>
void Book::reduce_from(M& book, Price price, Qty qty) noexcept {
    auto level = book.find(price);
    if(level == book.end()) return;
    RestingOrder& order = level->second.front();
    assert(order.remaining >= qty);
    if(order.remaining == qty) {
        std::uint64_t key = handle_key(order.client, order.client_order);
        by_handle.erase(key);
        level->second.pop_front();
        if(level->second.empty()) book.erase(price);
    } else {
        order.remaining -= qty;
    }
}

} // namespace ex
