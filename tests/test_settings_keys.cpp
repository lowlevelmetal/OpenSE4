// The Settings.txt keys the classic client honours (docs/spec/06 §1.9 "Music
// and Settings.txt", "Other Settings.txt keys"): music allowed, the Weapons
// Report's export, the system window's movement delay and the ending
// window's pictures; and when the ending window opens (§1.2.1, §1.7).

#include "engine_fixture.hpp"
#include "temp_dir.hpp"

#include "client/classic/data_export.hpp"
#include "client/classic/finale.hpp"
#include "client/classic/movement_replay.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/ship_glides.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "ruleset/ruleset.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;
using namespace opense4::client::classic;

namespace {

constexpr EmpireId kMe{0u};

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

size_t tabs(std::string_view line) { return static_cast<size_t>(std::count(line.begin(), line.end(), '\t')); }

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) out.push_back(l);
    return out;
}

} // namespace

TEST_CASE("settings keys: the loader keeps the client's Settings.txt keys") {
    // Settings.txt is read as generic keys: the client finds these through
    // rules.data().settings (an invented copy of the fixture data set).
    namespace fs = std::filesystem;
    const fs::path fixture = fs::path(OPENSE4_FIXTURE_DIR) / "minimal_dataset";
    const TempDir tmp("settings_keys");
    const fs::path dir = tmp.path() / "Data";
    fs::copy(fixture, dir);
    std::string text = readFile(dir / "Settings.txt");
    const size_t end = text.rfind("*END*");
    REQUIRE(end != std::string::npos);
    text.insert(end,
                "Allow CD Music := FALSE\n"
                "Allow Export of Weapon And Component Data := TRUE\n"
                "System Ship Movement Delay Milliseconds := 40\n"
                "Num Finale Lose Pictures := 3\n"
                "Finale Lose Picture 1 := TestFallA.bmp\n"
                "Finale Lose Picture 2 :=\n"
                "Finale Lose Picture 3 := TestFallC.bmp\n"
                "Num Finale Victory Pictures := 1\n"
                "Finale Victory Picture 1 := TestWin.bmp\n");
    std::ofstream(dir / "Settings.txt", std::ios::binary | std::ios::trunc) << text;
    const auto loaded = ruleset::loadRuleset(dir);
    REQUIRE(loaded.ruleset);
    const ruleset::Settings& s = loaded.ruleset->settings;
    CHECK_FALSE(musicAllowed(s));
    CHECK(exportAllowed(s));
    CHECK(s.integer("System Ship Movement Delay Milliseconds", 0) == 40);
    // An empty name is skipped; a kind without a list has no picture.
    CHECK(finalePictures(s, FinaleKind::Lose) == std::vector<std::string>{"Pictures/Game/Finale/TestFallA.bmp", "Pictures/Game/Finale/TestFallC.bmp"});
    CHECK(finalePictures(s, FinaleKind::Victory) == std::vector<std::string>{"Pictures/Game/Finale/TestWin.bmp"});
    CHECK(finalePictures(s, FinaleKind::HumanDead).empty());

    // Without the keys: music allowed, no export, no delay.
    const ruleset::Settings none;
    CHECK(musicAllowed(none));
    CHECK_FALSE(exportAllowed(none));
    CHECK(ShipGlides::stepPause(none.integer("System Ship Movement Delay Milliseconds", 0)) == 0.0);
    CHECK(ShipGlides::stepPause(40) == doctest::Approx(40.0));   // read as seconds, as the original does (§2.4)
    CHECK(ShipGlides::stepPause(-5) == 0.0);
}

TEST_CASE("settings keys: Allow CD Music in the Options window and the Combat Options lamp") {
    // Spec 06 §1.9: opening the Options window with music off, or not allowed,
    // stores music off at once; picking a volume lamp stores it on again.
    ClassicSettings s;
    s.musicOn = true;
    CHECK_FALSE(openMusicRows(s, true));   // allowed and on: nothing changes
    CHECK(s.musicOn);
    CHECK(openMusicRows(s, false));        // not allowed: off at once
    CHECK_FALSE(s.musicOn);
    CHECK_FALSE(openMusicRows(s, false));  // already off
    s.musicOn = true;                      // the player picks a volume lamp: stored on
    s.musicVolume = 60;
    CHECK_FALSE(openMusicRows(s, true));
    CHECK(s.musicVolume == 60);
    // Spec 06 §1.10.3: the "Music On" lamp is lit only when music is on and allowed.
    CHECK(musicLampLit(s, true));
    CHECK_FALSE(musicLampLit(s, false));
    s.musicOn = false;
    CHECK_FALSE(musicLampLit(s, true));
}

TEST_CASE("settings keys: the Weapons Report's export writes four tables") {
    const Rules& r = engineRules();
    const std::vector<ExportTable> tables = weaponAndComponentTables(r);
    REQUIRE(tables.size() == 4);
    CHECK(tables[0].fileName == "weapons.txt");
    CHECK(tables[1].fileName == "components.txt");
    CHECK(tables[2].fileName == "weapon_families.txt");
    CHECK(tables[3].fileName == "component_families.txt");
    // Every row has the header's columns.
    for (const ExportTable& t : tables) {
        const std::vector<std::string> rows = lines(t.text);
        REQUIRE_FALSE(rows.empty());
        for (const std::string& row : rows) CHECK(tabs(row) == tabs(rows.front()));
    }
    // One row per weapon (data order), one per component.
    const auto& comps = r.data().components;
    const auto weapons = std::count_if(comps.begin(), comps.end(), [](const ruleset::Component& c) { return c.isWeapon(); });
    REQUIRE(weapons > 0);
    const std::vector<std::string> weaponRows = lines(tables[0].text);
    CHECK(weaponRows.size() == static_cast<size_t>(weapons) + 1);
    CHECK(lines(tables[1].text).size() == comps.size() + 1);
    // The first weapon's row: its name, then its damage at range 1 among the last 20 columns.
    uint32_t first = 0;
    while (!comps[first].isWeapon()) ++first;
    const std::string& row = weaponRows[1];
    CHECK(row.starts_with(comps[first].name + "\t"));
    std::vector<std::string> cells;
    std::istringstream in(row);
    for (std::string c; std::getline(in, c, '\t');) cells.push_back(c);
    REQUIRE(cells.size() >= 20);
    CHECK(cells[cells.size() - 20] == std::to_string(weaponDamageAtRange(r, DesignEntry{first, -1}, 1)));
    CHECK(cells.back() == std::to_string(weaponDamageAtRange(r, DesignEntry{first, -1}, 20)));
    // Each family lists its members, lowest level first.
    for (const std::string& familyRow : lines(tables[3].text))
        CHECK(familyRow.find("\n") == std::string::npos);

    const TempDir tmp("weapon_export");
    const auto written = writeExportTables(tmp.path() / "SaveGame", "OpenSE4_", tables);
    REQUIRE(written);
    REQUIRE(written->size() == 4);
    CHECK(written->front().filename() == "OpenSE4_weapons.txt");
    for (size_t i = 0; i < tables.size(); ++i) CHECK(readFile((*written)[i]) == tables[i].text);
}

TEST_CASE("settings keys: the movement delay waits after each animated step") {
    // Spec 06 §1.9, §2.4 (confirmed: binary): with System Ship Movement Delay
    // Milliseconds above 0 the system window waits after each one-square
    // step, on the new square; the original reads the value as seconds. The
    // movement log replay never waits (§7 Q62).
    ShipGlides g;
    const VehicleId ship{1u};
    const SystemId sys{0u};
    const double pause = ShipGlides::stepPause(2);
    REQUIRE(pause == doctest::Approx(2.0));
    double now = 0.0;
    auto frame = [&](Location at) {
        const ShipGlides::Seen seen[] = {{ship, at, 2, true}};   // facing east throughout
        g.track(now, sys, true, seen, 50.0f, pause);
    };
    frame({sys, Sector{2, 2}});
    now = 1.0;
    frame({sys, Sector{4, 2}});  // two squares east: no turn, then the slide
    const ShipGlides::Glide* glide = g.find(ship);
    REQUIRE(glide);
    CHECK(glide->turnFrames == 0);
    CHECK(glide->slideFrames == 100);
    CHECK(glide->pause == doctest::Approx(2 * pause));   // after each of the two steps
    for (int i = 0; i < 100; ++i) {
        now += 0.0167;
        frame({sys, Sector{4, 2}});
    }
    // The slide is over; the ship waits on its new square.
    REQUIRE(g.find(ship));
    CHECK(ShipGlides::position(*g.find(ship)).x == doctest::Approx(4.5f));
    now += 2 * pause - 0.1;
    frame({sys, Sector{4, 2}});
    CHECK(g.find(ship));
    now += 0.2;
    frame({sys, Sector{4, 2}});
    CHECK_FALSE(g.find(ship));

    // The movement log replay animates a move without any pause: the next day
    // comes with the frame that ends the slide.
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const Location where = locationOf(s.galaxy, homeworld(s, kMe).planet);
    const VehicleId scout = addTestVehicle(s, r, addTestDesign(s, r, kMe, "Scout", "Test Frigate", {"Test Bridge"}), where).id;
    MovementRecorder rec(s);
    GameState day = s;
    Location east = where;
    east.sector.x = static_cast<decltype(east.sector.x)>(east.sector.x + 1);
    day.vehicle(scout)->location = east;
    rec.day(1, day);
    MovementReplay replay;
    replay.setLog(std::make_shared<MovementLog>(rec.take(3)));
    MovementReplay::Frame f;
    f.shown = where.system;
    f.animate = true;
    f.cellPixels = 50;
    f.turns = [](VehicleId) { return false; };
    replay.play();
    f.now = 10.0;
    replay.update(f);  // day 1 applied
    replay.update(f);  // its entry's first frame
    for (int i = 0; i < 49; ++i) {
        f.now += 0.0167;
        replay.update(f);
    }
    REQUIRE(replay.motion(scout));
    CHECK(replay.day() == 1);
    f.now += 0.0167;
    replay.update(f);  // the slide's last frame
    CHECK_FALSE(replay.motion(scout));
    CHECK(replay.day() == 2);
}

TEST_CASE("settings keys: which ending the game shows, once") {
    GameState s = newEngineGame(7, 2, 12, true);
    CHECK_FALSE(finaleKind(s, kMe, SessionKind::Local));
    // Every living empire handed to the computer: the local and hotseat games end.
    for (Empire& e : s.empires) e.kind = PlayerKind::Computer;
    CHECK(finaleKind(s, kMe, SessionKind::Local) == FinaleKind::HumanDead);
    CHECK(finaleKind(s, kMe, SessionKind::Hotseat) == FinaleKind::HumanDead);
    CHECK_FALSE(finaleKind(s, kMe, SessionKind::NetworkClient));   // a host on different machines has no such check
    // A dead human does not count as one.
    s.empires[0].kind = PlayerKind::Human;
    CHECK_FALSE(finaleKind(s, kMe, SessionKind::Local));
    s.empires[0].alive = false;
    CHECK(finaleKind(s, kMe, SessionKind::Local) == FinaleKind::HumanDead);
    // On a player's machine of a game on different machines, its own empire's fall.
    CHECK(finaleKind(s, kMe, SessionKind::NetworkClient) == FinaleKind::Lose);
    CHECK(finaleKind(s, kMe, SessionKind::Pbem) == FinaleKind::Lose);
    // The end by victory conditions comes first.
    s.gameOver = true;
    CHECK(finaleKind(s, kMe, SessionKind::Local) == FinaleKind::Victory);
    CHECK(finaleKind(s, EmpireId{}, SessionKind::NetworkClient) == FinaleKind::Victory);

    // Shown once per occurrence.
    GameState g = newEngineGame(7, 2, 12, true);
    FinaleWatch watch;
    CHECK_FALSE(watch.update(g, kMe, SessionKind::Local));
    g.gameOver = true;
    CHECK(watch.update(g, kMe, SessionKind::Local) == FinaleKind::Victory);
    CHECK_FALSE(watch.update(g, kMe, SessionKind::Local));
    CHECK_FALSE(watch.update(g, kMe, SessionKind::Local));
    CHECK(finaleKeyName(FinaleKind::HumanDead) == "Human Dead");
}
