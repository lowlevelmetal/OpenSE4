#pragma once

// The questions a script may ask about the game beyond its view
// (docs/sdk/view.md, "Queries"): routes, movement and supply range, a
// design's figures, abilities, construction and research forecasts. Each is a
// named function from an argument map to a result value, worked out by the
// engine's own functions on the perspective's state, so a fair view answers
// from what the empire knows. The runtime registers them by name.
//
// A query never changes the game. A bad argument comes back as a CodecError
// naming the argument ("vehicle: no such vehicle of ours").

#include "sdk/codec.hpp"
#include "sdk/view.hpp"

#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::sdk {

class Queries {
public:
    using Result = std::expected<script::Value, CodecError>;
    using Function = std::function<Result(const script::Value& args)>;
    struct Entry {
        std::string name;
        Function call;
    };

    explicit Queries(Perspective p);
    Queries(const game::Rules& r, const game::GameState& s, game::EmpireId empire, ViewOptions options = {});

    // Every query, by name, in a fixed order.
    std::span<const Entry> functions() const { return functions_; }
    // Runs the named query; an unknown name is an error.
    Result call(std::string_view name, const script::Value& args) const;
    const Perspective& perspective() const { return *p_; }

private:
    std::shared_ptr<const Perspective> p_;
    std::vector<Entry> functions_;
};

} // namespace opense4::sdk
