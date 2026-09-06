#pragma once
#include "book.hpp"
#include "command.hpp"
#include "event.hpp"
#include <vector>

namespace ex {

// The pure state machine: (state, sequenced command) -> (new state, output events).
class Engine {
public:
    // Validate, match against the opposite side, rest the remainder.
    // Appends to out; never clears it, so a caller can batch a whole session.
    void apply(const Command& cmd, std::vector<Event>& out);

    // For tests and invariant checks. The engine never exposes a mutable book.
    const Book& book() const noexcept { return book_; }

private:
    // Reject if off-tick / bad qty / duplicate handle, else cross then rest.
    void new_order(const Command& cmd, std::vector<Event>& out);

    // Remove by (client, client_order). Cancelled, or Rejected(UnknownOrder).
    void cancel(const Command& cmd, std::vector<Event>& out);

    Book book_;
};

} // namespace ex
