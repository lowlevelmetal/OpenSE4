#include "sdk/codec.hpp"

#include "sdk/value_io.hpp"

#include <array>
#include <utility>

namespace opense4::sdk {

using detail::Ctx;
using script::Value;

std::string CodecError::text() const { return path.empty() ? message : path + ": " + message; }

namespace detail {

namespace {

using Decoder = bool (*)(const ValueMap&, Ctx&, game::Command&);

template <size_t I>
bool decodeAlternative(const ValueMap& map, Ctx& c, game::Command& out) {
    using T = std::variant_alternative_t<I, game::Command>;
    T x{};
    Reader r(map, c, {"kind"});
    fields(r, x);
    if (!r.finish()) return false;
    out.emplace<I>(std::move(x));
    return true;
}

template <size_t... I>
constexpr std::array<Decoder, sizeof...(I)> decoders(std::index_sequence<I...>) {
    return {&decodeAlternative<I>...};
}
constexpr auto kDecoders = decoders(std::make_index_sequence<kCommandKinds>{});

} // namespace

Value Codec<game::Command>::enc(const game::Command& c) {
    return std::visit(
        [&](const auto& x) {
            Writer w;
            w.map.reserve(8);
            w.map.emplace_back("kind", Value(commandKindName(c)));
            fields(w, const_cast<std::remove_cvref_t<decltype(x)>&>(x));
            return Value(std::move(w.map));
        },
        c);
}

bool Codec<game::Command>::dec(const Value& v, Ctx& c, game::Command& out) {
    if (!v.isMap()) return c.fail("expected a command (a map with a kind)");
    std::optional<size_t> index;
    {
        const Ctx::Scope at = c.key("kind");
        const Value* kind = v.find("kind");
        if (!kind) return c.fail("missing: every command names its kind");
        if (!kind->isString()) return c.fail("expected a command kind name");
        index = commandKindIndex(kind->asString());
        if (!index) return c.fail(std::format("'{}' is not a command kind", kind->asString()));
    }
    return kDecoders[*index](v.asMap(), c, out);
}

} // namespace detail

namespace {

// Decodes `v` as a T with a fresh context.
template <class T>
Decoded<T> decodeAs(const Value& v) {
    Ctx c;
    T out{};
    if (!detail::dec(v, c, out)) return std::unexpected(c.takeError());
    return out;
}

} // namespace

Value encodeCommand(const game::Command& c) { return detail::enc(c); }

Decoded<game::Command> decodeCommand(const Value& v) {
    Ctx c;
    game::Command out;
    if (!detail::Codec<game::Command>::dec(v, c, out)) return std::unexpected(c.takeError());
    return out;
}

Value encodeCommands(std::span<const game::Command> commands) {
    script::ValueList l;
    l.reserve(commands.size());
    for (const game::Command& c : commands) l.push_back(encodeCommand(c));
    return Value(std::move(l));
}

Decoded<std::vector<game::Command>> decodeCommands(const Value& v) { return decodeAs<std::vector<game::Command>>(v); }

Value encodeOrder(const game::Order& o) { return detail::enc(o); }
Decoded<game::Order> decodeOrder(const Value& v) { return decodeAs<game::Order>(v); }

Value encodeEmpireOrders(const game::EmpireOrders& o) { return detail::enc(o); }
Decoded<game::EmpireOrders> decodeEmpireOrders(const Value& v) { return decodeAs<game::EmpireOrders>(v); }

Value encodeTacticalOrder(const game::combat::TacticalOrder& o) { return detail::enc(o); }
Decoded<game::combat::TacticalOrder> decodeTacticalOrder(const Value& v) { return decodeAs<game::combat::TacticalOrder>(v); }

} // namespace opense4::sdk
