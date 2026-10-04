#pragma once
#include "candidates.hpp"
namespace ea {
struct EnglishOption { int candidate, sense;std::wstring word,text;bool reference=false; };
std::vector<EnglishOption> english_options(const Snapshot& snapshot);
constexpr int page_size=9;
int move_selection(int current,int direction,int total,int first_visible);
}
