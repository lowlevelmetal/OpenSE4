#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "net/auth.hpp"
#include "temp_dir.hpp"

#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    // Nothing a test writes may reach the player's own user data folder
    // (settings, saved games, history): the run gets a scratch folder of its
    // own (client::userDataDirectory reads OPENSE4_USER_DIR).
    const opense4::test::TempDir userDir("userdata");
    const std::string dir = userDir.path().string();
#if defined(_WIN32)
    _putenv_s("OPENSE4_USER_DIR", dir.c_str());
#else
    setenv("OPENSE4_USER_DIR", dir.c_str(), 1);
#endif
    // Password keys take a moment of Argon2id each (net/auth.hpp); the tests
    // make hundreds, so they use less work. One test checks the real work.
    opense4::net::setPasswordWork({8 * 1024, 1});
    doctest::Context context(argc, argv);
    return context.run();
}
