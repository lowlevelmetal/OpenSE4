// A small program of ours that prints which user folder it would use and why
// (core/user_folder.hpp), as every program of OpenSE4 decides it.
// tests/test_user_folder.cpp copies it into a scratch program folder and runs
// it there, to check the portable copy's rules in a process of its own.

#include "core/user_folder.hpp"

#include <cstdio>
#include <string>

int main() {
    const opense4::core::UserFolder folder = opense4::core::userFolder();
    const std::u8string path = folder.path.u8string();
    const std::string source(opense4::core::userFolderSourceName(folder.source));
    std::printf("%s\n%s\n", source.c_str(), std::string(path.begin(), path.end()).c_str());
    return 0;
}
