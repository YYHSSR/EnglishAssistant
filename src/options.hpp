#pragma once
#include "candidates.hpp"
namespace ea {
struct EnglishOption { int candidate, sense;std::wstring word,text; };
std::vector<EnglishOption> english_options(const Snapshot& snapshot);
constexpr int page_size=9;
}
