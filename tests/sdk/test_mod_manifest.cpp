// Mod manifests (mod.toml) and versions (docs/sdk/packages-and-data.md).

#include "mods/manifest.hpp"
#include "mods/version.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace opense4;
using namespace opense4::mods;

namespace {

std::string errorsOf(std::string_view text) {
    auto m = parseManifest(text, "mods/x/mod.toml");
    if (m) return {};
    std::string out;
    for (const std::string& e : m.error()) out += e + "\n";
    return out;
}

} // namespace

TEST_CASE("sdk manifest: a complete mod.toml") {
    const auto m = parseManifest(R"([mod]
id = "example.better-carriers"
name = "Better Carriers"
version = "1.2.0"
api = 1
authors = ["Ada", "Grace"]
description = "Carriers that carry more."

[requires]
"example.common-lib" = ">=1.0"

[load]
after = ["example.common-lib", "example.other"]
)",
                                 "mod.toml");
    REQUIRE_MESSAGE(m, (m ? std::string{} : m.error().front()));
    CHECK(m->id == "example.better-carriers");
    CHECK(m->name == "Better Carriers");
    CHECK(m->version.text == "1.2.0");
    CHECK(m->version.parts == std::vector<uint32_t>{1, 2, 0});
    CHECK(m->api == 1);
    CHECK(m->authors == std::vector<std::string>{"Ada", "Grace"});
    CHECK(m->description == "Carriers that carry more.");
    REQUIRE(m->requirements.size() == 1);
    CHECK(m->requirements[0].id == "example.common-lib");
    CHECK(m->requirements[0].line == 10);
    CHECK(m->loadAfter == std::vector<std::string>{"example.common-lib", "example.other"});

    // Written out and read back, it is the same manifest.
    const auto back = parseManifest(writeManifest(*m), "written");
    REQUIRE(back);
    CHECK(back->id == m->id);
    CHECK(back->version == m->version);
    CHECK(back->authors == m->authors);
    CHECK(back->requirements.size() == 1);
    CHECK(back->loadAfter == m->loadAfter);
}

TEST_CASE("sdk manifest: every problem is reported with its line") {
    CHECK(errorsOf("[mod]\nname = \"x\"\nversion = \"1\"\napi = 1\n").find("needs an id") != std::string::npos);
    CHECK(errorsOf("[mod]\nid = \"Bad Id\"\nname = \"x\"\nversion = \"1\"\napi = 1\n").find("lowercase") != std::string::npos);
    CHECK(errorsOf("[mod]\nid = \"a.b\"\nname = \"x\"\nversion = \"1.x\"\napi = 1\n").find("mods/x/mod.toml:4: version '1.x'") != std::string::npos);
    CHECK(errorsOf("[mod]\nid = \"a.b\"\nname = \"x\"\nversion = \"1\"\n").find("SDK interface version") != std::string::npos);
    CHECK(errorsOf("[mod]\nid = \"a.b\"\nname = \"x\"\nversion = \"1\"\napi = 2\n").find("needs a newer OpenSE4") != std::string::npos);
    // Typos are errors.
    CHECK(errorsOf("[mod]\nid = \"a.b\"\nname = \"x\"\nversion = \"1\"\napi = 1\nauthor = \"me\"\n").find("mod.toml:6: unknown key 'author'") !=
          std::string::npos);
    CHECK(errorsOf("[mod]\nid = \"a.b\"\nname = \"x\"\nversion = \"1\"\napi = 1\n[require]\n").find("unknown table 'require'") != std::string::npos);
    CHECK(errorsOf("[mod]\nid = \"a.b\"\nname = \"x\"\nversion = \"1\"\napi = 1\n[requires]\n\"c.d\" = \"about 2\"\n").find("version range") !=
          std::string::npos);
    CHECK(errorsOf("[mod]\nid = \"a.b\"\nname = \"x\"\nversion = \"1\"\napi = 1\n[load]\nbefore = [\"c\"]\n").find("unknown key 'before'") !=
          std::string::npos);
    CHECK(errorsOf("[mod\n").find("mods/x/mod.toml:1") != std::string::npos);
    CHECK(errorsOf("title = \"x\"\n").find("no [mod] table") != std::string::npos);
}

TEST_CASE("sdk manifest: mod ids") {
    CHECK(validModId("example.better-carriers"));
    CHECK(validModId("a"));
    CHECK(validModId("mod_2.v3"));
    CHECK_FALSE(validModId(""));
    CHECK_FALSE(validModId(".hidden"));
    CHECK_FALSE(validModId("Upper"));
    CHECK_FALSE(validModId("with space"));
    CHECK_FALSE(validModId(std::string(65, 'a')));
}

TEST_CASE("sdk versions: comparison and ranges") {
    auto v = [](std::string_view s) { return *parseVersion(s); };
    CHECK(v("1.2") == v("1.2.0"));
    CHECK(v("1.10") > v("1.9"));
    CHECK(v("2") > v("1.99.99"));
    CHECK_FALSE(parseVersion(""));
    CHECK_FALSE(parseVersion("1..2"));
    CHECK_FALSE(parseVersion("1.2.3.4.5"));
    CHECK_FALSE(parseVersion("v1"));

    auto in = [&](std::string_view range, std::string_view version) {
        auto r = parseVersionRange(range);
        REQUIRE_MESSAGE(r, (r ? std::string{} : r.error()));
        return r->contains(v(version));
    };
    CHECK(in(">=1.0", "1.0"));
    CHECK(in(">=1.0", "3"));
    CHECK_FALSE(in(">=1.0", "0.9"));
    CHECK(in(">=1.0, <2", "1.9.9"));
    CHECK_FALSE(in(">=1.0, <2", "2.0"));
    CHECK(in(">=1.0 <2", "1.5"));
    CHECK(in("1.2", "1.2.7"));
    CHECK_FALSE(in("1.2", "1.3"));
    CHECK(in("=1.2.3", "1.2.3"));
    CHECK_FALSE(in("==1.2.3", "1.2.4"));
    CHECK(in("^1.2", "1.9"));
    CHECK_FALSE(in("^1.2", "2.0"));
    CHECK_FALSE(in("^1.2", "1.1"));
    CHECK(in("~1.2", "1.2.9"));
    CHECK_FALSE(in("~1.2", "1.3"));
    CHECK(in("*", "0.0.1"));
    CHECK(in(">1, <=3", "3"));
    CHECK_FALSE(in(">1, <=3", "1"));
    CHECK_FALSE(parseVersionRange(""));
    CHECK_FALSE(parseVersionRange(">="));
    CHECK_FALSE(parseVersionRange("newest"));
}
