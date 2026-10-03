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
    // Spec 06 §7 Q83 (confirmed: binary): Weapons.txt, Comps.txt,
    // WeaponFamilies.txt, CompFamilies.txt, fixed-width columns under a header
    // line and a line of dashes, every weapon or every component in data order.
    const Rules& r = engineRules();
    const std::vector<ExportTable> tables = weaponAndComponentTables(r);
    REQUIRE(tables.size() == 4);
    CHECK(tables[0].fileName == "Weapons.txt");
    CHECK(tables[1].fileName == "Comps.txt");
    CHECK(tables[2].fileName == "WeaponFamilies.txt");
    CHECK(tables[3].fileName == "CompFamilies.txt");
    for (const ExportTable& t : tables) {
        const std::vector<std::string> rows = lines(t.text);
        REQUIRE(rows.size() >= 2);
        CHECK(rows[0].starts_with("Name"));
        CHECK(rows[1] == std::string(rows[0].size(), '-'));
        for (const std::string& row : rows) CHECK(row.find('\t') == std::string::npos);
    }
    const auto& comps = r.data().components;
    const auto weapons = std::count_if(comps.begin(), comps.end(), [](const ruleset::Component& c) { return c.isWeapon(); });
    REQUIRE(weapons > 0);
    const std::vector<std::string> weaponRows = lines(tables[0].text);
    CHECK(weaponRows.size() == static_cast<size_t>(weapons) + 2);
    CHECK(lines(tables[1].text).size() == comps.size() + 2);
    CHECK(lines(tables[2].text).size() == static_cast<size_t>(weapons) + 2);
    CHECK(lines(tables[3].text).size() == comps.size() + 2);
    // The first weapon's row: its name in 40 columns, then the damage at
    // ranges 1 to 20 in 4 columns each.
    uint32_t first = 0;
    while (!comps[first].isWeapon()) ++first;
    const std::string& row = weaponRows[2];
    REQUIRE(row.size() > 40 + 80);
    CHECK(row.substr(0, comps[first].name.size()) == comps[first].name);
    auto field = [&](size_t at, size_t width) {
        std::string f = row.substr(at, width);
        f.erase(0, f.find_first_not_of(' '));
        return f;
    };
    CHECK(field(40, 4) == std::to_string(weaponDamageAtRange(r, DesignEntry{first, -1}, 1)));
    CHECK(field(40 + 19 * 4, 4) == std::to_string(weaponDamageAtRange(r, DesignEntry{first, -1}, 20)));
    // Reload rate and tonnage follow, 6 columns each.
    CHECK(field(120, 6) == std::to_string(comps[first].weapon.reloadRate));
    CHECK(field(126, 6) == std::to_string(comps[first].tonnage));
    // A component row: tonnage and structure after the 40-column name.
    const std::string comp = lines(tables[1].text)[2];
    CHECK(comp.substr(0, comps[0].name.size()) == comps[0].name);
    CHECK(comp.substr(40, 6).find(std::to_string(comps[0].tonnage)) != std::string::npos);

    const TempDir tmp("weapon_export");
    const auto written = writeExportTables(tmp.path() / "SaveGame", "", tables);
    REQUIRE(written);
    REQUIRE(written->size() == 4);
    CHECK(written->front().filename() == "Weapons.txt");
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

TEST_CASE("settings keys: which endings the game shows, each as it comes") {
    // Spec 06 §7 Q83 (confirmed: binary).
    using K = FinaleKind;
    using List = std::vector<FinaleKind>;
    GameState s = newEngineGame(7, 2, 12, true);
    CHECK(finaleKinds(s, kMe, SessionKind::Local).empty());
    // Every living empire handed to the computer: the local and hotseat games end.
    for (Empire& e : s.empires) e.kind = PlayerKind::Computer;
    CHECK(finaleKinds(s, kMe, SessionKind::Local) == List{K::HumanDead});
    CHECK(finaleKinds(s, kMe, SessionKind::Hotseat) == List{K::HumanDead});
    CHECK(finaleKinds(s, kMe, SessionKind::NetworkClient).empty());   // a host on different machines has no such check
    s.empires[0].kind = PlayerKind::Human;
    CHECK(finaleKinds(s, kMe, SessionKind::Local).empty());
    // The player's empire owns nothing but is not yet marked dead: Lose, the
    // start of its last turn; on any kind of machine.
    for (auto& c : s.colonies)
        if (c && c->owner == kMe) c.reset();
    for (Vehicle& v : s.vehicles)
        if (v.owner == kMe) v.count = 0;
    s.removeDeadVehicles();
    CHECK(finaleKinds(s, kMe, SessionKind::Local) == List{K::Lose});
    CHECK(finaleKinds(s, kMe, SessionKind::NetworkClient) == List{K::Lose});
    // Marked dead at the end of that turn: in a local game no human is left.
    s.empires[0].alive = false;
    CHECK(finaleKinds(s, kMe, SessionKind::Local) == List{K::HumanDead});
    CHECK(finaleKinds(s, kMe, SessionKind::Pbem).empty());
    // Several in one turn change, in order: Lose, Victory, then the conquest.
    s.empires[0].alive = true;
    s.empires[1].alive = false;
    s.gameOver = true;
    CHECK(finaleKinds(s, kMe, SessionKind::Local) == List{K::Lose, K::Victory, K::Conquered});
    CHECK(finaleKinds(s, EmpireId{}, SessionKind::NetworkClient) == List{K::Victory});

    // Each shown once, at a turn's start, while it keeps holding.
    GameState g = newEngineGame(7, 2, 12, true);
    FinaleWatch watch;
    CHECK(watch.update(g, kMe, SessionKind::Local).empty());
    g.gameOver = true;
    CHECK(watch.update(g, kMe, SessionKind::Local).empty());  // not a new turn
    ++g.turn;
    CHECK(watch.update(g, kMe, SessionKind::Local) == List{K::Victory});
    CHECK(watch.update(g, kMe, SessionKind::Local).empty());
    g.empires[1].alive = false;
    ++g.turn;
    CHECK(watch.update(g, kMe, SessionKind::Local) == List{K::Conquered});  // Victory still holds: not again
    ++g.turn;
    CHECK(watch.update(g, kMe, SessionKind::Local).empty());
    CHECK(finaleKeyName(FinaleKind::HumanDead) == "Human Dead");
    CHECK(finaleKeyName(FinaleKind::Conquered) == "Victory");
}
