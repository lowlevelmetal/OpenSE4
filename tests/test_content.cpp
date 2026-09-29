#include "test_support.hpp"

#include "sim/rules.hpp"

#include <filesystem>
#include <fstream>

using namespace opense4;
using namespace opense4::sim;

TEST_CASE("shipped game data loads and cross-references resolve") {
    const Content& c = test::content();
    CHECK(c.techs.size() >= 5);
    CHECK(c.hulls.size() >= 3);
    CHECK(c.races.size() >= 4);
    CHECK(c.findComponent("ion_engine").has_value());
    CHECK(c.findFacility("space_yard").has_value());
    CHECK(c.findDesignTemplate("Scout") != nullptr);
    CHECK_FALSE(c.rules.requiredShipAbilities.empty());

    const auto propulsion = c.findTech("propulsion");
    REQUIRE(propulsion);
    const auto physics = c.findTech("physics");
    REQUIRE(physics);
    REQUIRE(c.tech(*propulsion).prerequisites.size() == 1);
    CHECK(c.tech(*propulsion).prerequisites[0].tech == *physics);
}

TEST_CASE("starting designs are valid for every race") {
    const Content& c = test::content();
    for (const RaceDef& race : c.races) {
        for (const DesignTemplate& tpl : c.designTemplates) {
            std::vector<ComponentIndex> comps;
            for (ComponentIndex ci : tpl.components) comps.push_back(ci.valid() ? ci : race.colonyComponent);
            const DesignStats st = computeDesignStats(c, tpl.hull, comps);
            INFO("race " << race.key << ", design " << tpl.name);
            CHECK(st.problems.empty());
            CHECK(st.speed > 0);
            if (tpl.role == "colony") CHECK(st.canColonize(race.nativeSurface));
            if (tpl.role == "warship") CHECK(st.armed());
        }
    }
}

TEST_CASE("data errors are reported with file and field context") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "opense4_bad_data";
    fs::remove_all(dir);
    fs::copy(OPENSE4_DATA_DIR, dir, fs::copy_options::recursive);
    {
        std::ofstream out(dir / "hulls.toml", std::ios::app);
        out << "\n[[hull]]\nkey = \"broken\"\nname = \"Broken\"\nsize = 100\nstructure = 10\n"
               "cost = { minerals = 5, unobtainium = 3 }\nengines_per_move = 1\nmax_engines = 2\nspeeed = 4\n"
               "requires = { warp_theory = 1 }\n"
               "\n[[hull]]\nkey = \"unreachable\"\nname = \"Unreachable\"\nsize = 100\nstructure = 10\n"
               "cost = { minerals = 5 }\nengines_per_move = 1\nmax_engines = 2\nrequires = { propulsion = 99 }\n";
    }
    {
        std::ofstream out(dir / "techs.toml", std::ios::app);
        out << "\n[[tech]]\nkey = \"chicken\"\nname = \"Chicken\"\nmax_level = 1\nbase_cost = 10\nrequires = { egg = 1 }\n"
               "\n[[tech]]\nkey = \"egg\"\nname = \"Egg\"\nmax_level = 1\nbase_cost = 10\nrequires = { chicken = 1 }\n";
    }
    const auto result = loadContent(dir);
    fs::remove_all(dir);
    REQUIRE_FALSE(result.has_value());

    auto mentions = [&](std::string_view needle) {
        return std::any_of(result.error().begin(), result.error().end(),
                           [&](const std::string& e) { return e.find(needle) != std::string::npos; });
    };
    CHECK(mentions("unknown field 'speeed'"));
    CHECK(mentions("unknown resource 'unobtainium'"));
    CHECK(mentions("unknown tech 'warp_theory'"));
    CHECK(mentions("hulls.toml"));
    CHECK(mentions("prerequisite cycle"));
    CHECK(mentions("requires propulsion level 99, but its max_level is 8"));
}
