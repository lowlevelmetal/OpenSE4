// The SDK's docs are its schema (docs/sdk/commands.md): these tests fail when
// a command kind, an order kind, an enumeration value or a field is missing
// from them, or when they describe one the code does not make.

#include "command_samples.hpp"
#include "engine_fixture.hpp"
#include "sdk/sdk_test_util.hpp"

#include "sdk/codec.hpp"
#include "sdk/names.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

using namespace opense4;
using namespace opense4::game;
using opense4::script::Value;
using namespace opense4::sdktest;

namespace {

template <class E>
void checkEnum(std::string_view docName) {
    INFO("enumeration `" << docName << "`");
    const Schema::Section* s = docsSchema().find(docName);
    REQUIRE_MESSAGE(s != nullptr, "the docs have no table for `" << docName << "`");
    CHECK(s->isEnum);
    std::vector<std::string> names;
    for (std::string_view n : sdk::enumNames<E>()) names.emplace_back(n);
    CHECK(s->values == names);
}

std::string readDoc(std::string_view name) {
    std::ifstream in(std::filesystem::path(OPENSE4_DOCS_DIR) / "sdk" / name);
    std::stringstream out;
    out << in.rdbuf();
    return out.str();
}

template <size_t... I>
std::vector<Command> defaultCommands(std::index_sequence<I...>) {
    return {Command{std::in_place_index<I>}...};
}

} // namespace

TEST_CASE("sdk docs: the schema tables read cleanly") {
    const Schema& s = docsSchema();
    CHECK_MESSAGE(s.problems.empty(), joined(s.problems));
    CHECK(s.sections.size() > 80);
}

TEST_CASE("sdk docs: every command kind has its section, its fields and an example") {
    const Schema& schema = docsSchema();
    const std::string text = readDoc("commands.md");
    for (std::string_view kind : sdk::commandKindNames()) {
        INFO(kind);
        const Schema::Section* s = schema.find(kind);
        REQUIRE_MESSAGE(s != nullptr, "commands.md has no section for `" << kind << "`");
        CHECK_FALSE(s->isEnum);
        CHECK_MESSAGE(text.find(std::format("\"kind\": \"{}\"", kind)) != std::string::npos, "commands.md has no example of " << kind);
    }
    // The samples, and every command with its defaults (ids null, lists empty).
    std::vector<Command> commands = test::everyCommandSample(test::engineRules()).commands;
    for (Command& c : defaultCommands(std::make_index_sequence<sdk::kCommandKinds>{})) commands.push_back(std::move(c));
    for (const Command& c : commands) {
        INFO(sdk::commandKindName(c));
        const auto problems = validate(schema, sdk::encodeCommand(c), "command");
        CHECK_MESSAGE(problems.empty(), joined(problems));
    }
    const auto turn = validate(schema, sdk::encodeEmpireOrders(test::everyCommandSample(test::engineRules())), "empire_orders");
    CHECK_MESSAGE(turn.empty(), joined(turn));
}

TEST_CASE("sdk docs: every order kind and tactical order kind is documented, with an example") {
    const Schema& schema = docsSchema();
    const std::string text = readDoc("commands.md");
    for (size_t k = 0; k < static_cast<size_t>(OrderKind::Count); ++k) {
        Order o;
        o.kind = static_cast<OrderKind>(k);
        o.location = {SystemId{1u}, Sector{2, 3}};
        o.vehicle = VehicleId{4u};
        const std::string_view name = sdk::enumName(o.kind);
        INFO(name);
        const auto problems = validate(schema, sdk::encodeOrder(o), "order");
        CHECK_MESSAGE(problems.empty(), joined(problems));
        CHECK_MESSAGE(text.find(std::format("\"kind\": \"{}\"", name)) != std::string::npos, "commands.md has no example of " << name);
    }
    using K = combat::TacticalOrder::Kind;
    for (size_t k = 0; k <= static_cast<size_t>(K::ResolveCombat); ++k) {
        combat::TacticalOrder o;
        o.kind = static_cast<K>(k);
        o.path = {{1, 2}};
        const std::string_view name = sdk::enumName(o.kind);
        INFO(name);
        const auto problems = validate(schema, sdk::encodeTacticalOrder(o), "tactical_order");
        CHECK_MESSAGE(problems.empty(), joined(problems));
        CHECK_MESSAGE(text.find(std::format("\"kind\": \"{}\"", name)) != std::string::npos, "commands.md has no example of " << name);
    }
}

TEST_CASE("sdk docs: every enumeration lists exactly its names, in order") {
    checkEnum<OrderKind>("order_kind");
    checkEnum<StellarAction>("stellar_action");
    checkEnum<Resource>("resource");
    checkEnum<Treaty>("treaty");
    checkEnum<MessageType>("message_type");
    checkEnum<PackageItem::Kind>("package_item_kind");
    checkEnum<QueueItem::Kind>("queue_item_kind");
    checkEnum<cmd::DemandList>("demand_list");
    checkEnum<EncounterClear>("encounter_clear");
    checkEnum<Minister>("minister");
    checkEnum<combat::TacticalOrder::Kind>("tactical_order_kind");
}
