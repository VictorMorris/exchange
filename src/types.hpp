#pragma once
#include <cstdint>
#include <utility>

namespace ex {

// Scaled fixed-point
// Engine must never multiply price by quantity for danger of int overflow
inline constexpr std::int64_t PriceScale = 10'000;      // 1 unit = 1e-4 quote
inline constexpr std::int64_t QtyScale   = 100'000'000; // 1 unit = 1e-8 base (Btcs Scale)

// Prices off this grid are rejected by the engine, not the edge
inline constexpr std::int64_t TickSize = 100;           // $0.01

enum class Price   : std::int64_t {};
enum class Qty     : std::int64_t {};
enum class OrderId : std::uint64_t {};   // == Seq of the command that created it
enum class ClientId : std::uint32_t {};
enum class Seq     : std::uint64_t {};

enum class Side : std::uint8_t { Bid = 0, Ask = 1 };

// Opaque, client-assigned, unique per client
// The client's cancel handle, usable before an ack arrives
enum class ClientOrderId : std::uint32_t {};

// enum class blocks implicit conversion but also arithmetic
constexpr std::int64_t raw(Price p) noexcept { return std::to_underlying(p); }
constexpr std::int64_t raw(Qty q) noexcept { return std::to_underlying(q); }

constexpr Qty operator+ (Qty a, Qty b) noexcept { return Qty{raw(a) + raw(b)}; }
constexpr Qty operator- (Qty a, Qty b) noexcept { return Qty{raw(a) - raw(b)}; }
constexpr Qty& operator+= (Qty& a, Qty b) noexcept  {a = a + b; return a; }
constexpr Qty& operator-= (Qty& a, Qty b) noexcept  {a = a - b; return a; }




constexpr bool valid_price(Price p) noexcept { return !(raw(p) % TickSize) && (raw(p) >= 0); };
constexpr Side opposite(Side s) noexcept { return s == Side::Bid ? Side::Ask : Side::Bid; };

// Takes the 32 bit ClientId, widens to 64 bits, shifts to upper half, OR the ClientOrderId to the bottom half
constexpr std::uint64_t handle_key(ClientId c, ClientOrderId h) noexcept {
    return (static_cast<std::uint64_t>(std::to_underlying(c)) << 32) | std::to_underlying(h);
}

// True when `taker` at this price would cross a resting order at `maker`.
constexpr bool crosses(Side taker_side, Price taker, Price maker) noexcept {
    return taker_side == Side::Bid ? raw(taker) >= raw(maker) : raw(taker) <= raw(maker);
};

} // namespace ex
