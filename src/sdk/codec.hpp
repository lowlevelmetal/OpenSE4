#pragma once

// The command codec of the modding SDK (docs/sdk/commands.md): game commands
// as script values and back. A command is a map whose `kind` names it
// ("set_orders") and whose other keys are its fields in lower_snake_case;
// ids are whole numbers (null for none), enumerations their names
// (sdk/names.hpp).
//
// Decoding checks shapes only: kinds, field names, value types and ranges.
// Whether the command is allowed is game::apply's to say, exactly as for a
// human player's command. A missing field keeps its default (an id: none), an
// unknown one is an error, and every error names the path to the bad value
// ("orders[2].object: expected an object id").
//
// Pure: encoding and decoding read nothing but their argument.

#include "game/commands.hpp"
#include "game/tactical.hpp"
#include "script/value.hpp"

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace opense4::sdk {

struct CodecError {
    std::string path;      // e.g. "orders[2].object"; empty: the value itself
    std::string message;   // e.g. "expected an object id"
    // "orders[2].object: expected an object id"
    std::string text() const;
};

template <class T>
using Decoded = std::expected<T, CodecError>;

script::Value encodeCommand(const game::Command& c);
Decoded<game::Command> decodeCommand(const script::Value& v);

// A list of commands; an error's path starts with the command's position.
script::Value encodeCommands(std::span<const game::Command> commands);
Decoded<std::vector<game::Command>> decodeCommands(const script::Value& v);

// One order of an order list (cmd::SetOrders, cmd::OrderTagged, the view).
script::Value encodeOrder(const game::Order& o);
Decoded<game::Order> decodeOrder(const script::Value& v);

// One empire's turn: {"empire", "turn", "commands"}.
script::Value encodeEmpireOrders(const game::EmpireOrders& o);
Decoded<game::EmpireOrders> decodeEmpireOrders(const script::Value& v);

// Orders given in a tactical battle (combat::TacticalBattle::submit).
script::Value encodeTacticalOrder(const game::combat::TacticalOrder& o);
Decoded<game::combat::TacticalOrder> decodeTacticalOrder(const script::Value& v);

} // namespace opense4::sdk
