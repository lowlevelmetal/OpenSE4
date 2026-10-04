#pragma once

// Windows builds with libc++ only (OPENSE4_LIBCXX_UTF8_PATHS, cmake/WindowsCompat.cmake):
// std::filesystem converts narrow strings as UTF-8 (libcxx_utf8_paths.cpp).

namespace opense4::compat {

// How many conversions between a path and a narrow string went through
// libcxx_utf8_paths.cpp: the tests check that the linker wired it in.
unsigned utf8PathConversions();

} // namespace opense4::compat
