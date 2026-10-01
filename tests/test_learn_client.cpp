// The client's side of the learning system: its window ids match the
// vocabulary lessons are checked against, and progress survives the
// settings file.

#include "client/classic/screen_id.hpp"
#include "client/classic/settings.hpp"
#include "learn/ids.hpp"

#include <doctest/doctest.h>

using namespace opense4;
using namespace opense4::client::classic;

TEST_CASE("learn client: every window has the id lessons use") {
    const auto windows = learn::windows();
    REQUIRE(windows.size() == static_cast<size_t>(ScreenId::Count));
    for (size_t i = 0; i < windows.size(); ++i) {
        const auto id = static_cast<ScreenId>(i);
        CHECK(windowId(id) == windows[i].id);
        CHECK(screenFromWindowId(windows[i].id) == id);
        CHECK(learn::isUiTag("window:" + std::string(windowId(id))));
    }
    CHECK(screenFromWindowId("learn") == ScreenId::Learn);
    CHECK_FALSE(screenFromWindowId("nothing"));
}

TEST_CASE("learn client: finished lessons are kept with the client settings") {
    ClassicSettings s;
    s.learnDone = {"tutorial:first-steps", "training:expansion"};
    const ClassicSettings back = settingsFromToml(settingsToToml(s));
    CHECK(back.learnDone == s.learnDone);
    CHECK(settingsFromToml("").learnDone.empty());
}
