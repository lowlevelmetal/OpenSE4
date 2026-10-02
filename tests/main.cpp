#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

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
    doctest::Context context(argc, argv);
    return context.run();
}
