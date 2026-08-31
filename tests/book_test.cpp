#include "check.hpp"
#include "book.hpp"

using namespace ex;

namespace {

// Terse constructor for test fixtures
// id doubles as the (client, handle) pair
RestingOrder ord(std::uint64_t id, std::int64_t price, std::int64_t qty, Side side) {
    return RestingOrder{OrderId{id}, ClientId{1}, ClientOrderId{static_cast<std::uint32_t>(id)},
                        Price{price}, Qty{qty}, side};
}

void empty_book_has_no_best() {
    Book b;
    CHECK(!b.best_bid().has_value());
    CHECK(!b.best_ask().has_value());

    std::vector<Book::Level> out;
    b.levels(Side::Bid, out);
    CHECK(out.empty());
}

void best_is_highest_bid_and_lowest_ask() {
    Book b;
    b.rest(ord(1, 100'00, 5, Side::Bid));
    b.rest(ord(2, 102'00, 5, Side::Bid));
    b.rest(ord(3, 101'00, 5, Side::Bid));
    b.rest(ord(4, 105'00, 5, Side::Ask));
    b.rest(ord(5, 103'00, 5, Side::Ask));

    CHECK(raw(b.best_bid().value()) == 102'00);
    CHECK(raw(b.best_ask().value()) == 103'00);
}

void levels_aggregate_per_price_and_sort_best_first() {
    Book b;
    b.rest(ord(1, 100'00, 3, Side::Bid));
    b.rest(ord(2, 100'00, 4, Side::Bid));   // same level, should sum to 7
    b.rest(ord(3,  99'00, 2, Side::Bid));
    b.rest(ord(4, 101'00, 6, Side::Ask));

    std::vector<Book::Level> out;
    b.levels(Side::Bid, out);
    CHECK(out.size() == 2);
    CHECK(raw(out[0].price) == 100'00);     // bids descending
    CHECK(raw(out[0].qty)   == 7);
    CHECK(raw(out[1].price) == 99'00);

    b.levels(Side::Ask, out);               // must clear, not append
    CHECK(out.size() == 1);
    CHECK(raw(out[0].price) == 101'00);
}

void front_is_earliest_order_at_price() {
    Book b;
    b.rest(ord(1, 100'00, 3, Side::Bid));
    b.rest(ord(2, 100'00, 4, Side::Bid));

    const RestingOrder* f = b.front(Side::Bid, Price{100'00});
    CHECK(f != nullptr);
    CHECK(std::to_underlying(f->id) == 1);       
    CHECK(b.front(Side::Bid, Price{99'00}) == nullptr);
}

// remove checks

void remove_returns_the_order_and_drops_empty_level() {
    Book b;
    b.rest(ord(1, 100'00, 5, Side::Bid));

    auto r = b.remove(ClientId{1}, ClientOrderId{1});
    CHECK(r.has_value());
    CHECK(std::to_underlying(r->id) == 1);
    CHECK(raw(r->price) == 100'00);
    CHECK(raw(r->remaining) == 5);
    CHECK(r->side == Side::Bid);

    CHECK(!b.best_bid().has_value());
    std::vector<Book::Level> out;
    b.levels(Side::Bid, out);
    CHECK(out.empty());
}

void remove_unknown_handle_returns_nullopt() {
    Book b;
    CHECK(!b.remove(ClientId{1}, ClientOrderId{1}).has_value());   // empty book

    b.rest(ord(1, 100'00, 5, Side::Bid));
    CHECK(!b.remove(ClientId{1}, ClientOrderId{2}).has_value());   // right client, wrong handle
    CHECK(!b.remove(ClientId{2}, ClientOrderId{1}).has_value());   // wrong client, right handle
    CHECK(b.best_bid().has_value());                               // neither miss touched the book
}

void remove_keeps_other_orders_at_the_same_price() {
    Book b;
    b.rest(ord(1, 100'00, 3, Side::Bid));
    b.rest(ord(2, 100'00, 4, Side::Bid));

    CHECK(b.remove(ClientId{1}, ClientOrderId{2}).has_value());    // erase from the middle/back

    const RestingOrder* f = b.front(Side::Bid, Price{100'00});
    CHECK(f != nullptr);
    CHECK(std::to_underlying(f->id) == 1);                         // survivor keeps its queue position

    std::vector<Book::Level> out;
    b.levels(Side::Bid, out);
    CHECK(out.size() == 1);
    CHECK(raw(out[0].qty) == 3);
}

// reduce_front checks

void reduce_front_partial_leaves_remainder() {
    Book b;
    b.rest(ord(1, 100'00, 5, Side::Bid));
    b.reduce_front(Side::Bid, Price{100'00}, Qty{2});

    const RestingOrder* f = b.front(Side::Bid, Price{100'00});
    CHECK(f != nullptr);
    CHECK(std::to_underlying(f->id) == 1);                         // same order, still at the front
    CHECK(raw(f->remaining) == 3);

    std::vector<Book::Level> out;
    b.levels(Side::Bid, out);
    CHECK(out.size() == 1);
    CHECK(raw(out[0].qty) == 3);
}

void reduce_front_exact_pops_the_order() {
    Book b;
    b.rest(ord(1, 100'00, 3, Side::Bid));
    b.rest(ord(2, 100'00, 4, Side::Bid));
    b.reduce_front(Side::Bid, Price{100'00}, Qty{3});

    const RestingOrder* f = b.front(Side::Bid, Price{100'00});
    CHECK(f != nullptr);
    CHECK(std::to_underlying(f->id) == 2);                         // next in FIFO takes over
    CHECK(raw(f->remaining) == 4);
    CHECK(raw(b.best_bid().value()) == 100'00);                    // level survives

    std::vector<Book::Level> out;
    b.levels(Side::Bid, out);
    CHECK(out.size() == 1);
    CHECK(raw(out[0].qty) == 4);
}

void reduce_front_draining_level_removes_it() {
    Book b;
    b.rest(ord(1, 100'00, 5, Side::Bid));
    b.rest(ord(2,  99'00, 5, Side::Bid));
    b.reduce_front(Side::Bid, Price{100'00}, Qty{5});

    CHECK(b.front(Side::Bid, Price{100'00}) == nullptr);
    CHECK(raw(b.best_bid().value()) == 99'00);                     // best moves down a level

    std::vector<Book::Level> out;
    b.levels(Side::Bid, out);
    CHECK(out.size() == 1);
    CHECK(raw(out[0].price) == 99'00);
}

// index lifetime

void removed_handle_can_be_reused() {
    Book b;

    // released by remove()
    b.rest(ord(1, 100'00, 5, Side::Bid));
    CHECK(b.remove(ClientId{1}, ClientOrderId{1}).has_value());
    b.rest(ord(1, 101'00, 7, Side::Bid));            // asserts inside rest_in if the key leaked
    CHECK(raw(b.best_bid().value()) == 101'00);

    // released by reduce_front() popping the order
    b.reduce_front(Side::Bid, Price{101'00}, Qty{7});
    CHECK(!b.best_bid().has_value());
    b.rest(ord(1, 102'00, 2, Side::Bid));
    CHECK(raw(b.best_bid().value()) == 102'00);
    CHECK(b.remove(ClientId{1}, ClientOrderId{1}).has_value());    // and still addressable
}

} // namespace

int main() {
    RUN(empty_book_has_no_best);
    RUN(best_is_highest_bid_and_lowest_ask);
    RUN(levels_aggregate_per_price_and_sort_best_first);
    RUN(front_is_earliest_order_at_price);

    RUN(remove_returns_the_order_and_drops_empty_level);
    RUN(remove_unknown_handle_returns_nullopt);
    RUN(remove_keeps_other_orders_at_the_same_price);
    RUN(reduce_front_partial_leaves_remainder);
    RUN(reduce_front_exact_pops_the_order);
    RUN(reduce_front_draining_level_removes_it);
    RUN(removed_handle_can_be_reused);

    std::printf("\n%d checks, %d failures\n", test::checks, test::failures);
    return test::failures != 0;
}
