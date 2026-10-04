#pragma once
// Ubuntu 18.04 ships GCC 7's experimental filesystem implementation.
#if defined(__GNUC__) && __GNUC__ < 8 && !defined(_WIN32)
#include <experimental/filesystem>
namespace ea { namespace fs = std::experimental::filesystem; }
#else
#include <filesystem>
namespace ea { namespace fs = std::filesystem; }
#endif
