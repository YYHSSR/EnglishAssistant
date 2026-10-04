#pragma once
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include "filesystem_compat.hpp"

namespace ea {
std::wstring wide(std::string_view text);
std::string utf8(std::wstring_view text);
fs::path native_path(std::wstring_view text);
std::vector<std::wstring> parse_senses(std::string_view fields);

// The TSV stays mapped; the only per-entry allocation is a compact line index.
class Dictionary {
    struct Line { uint32_t start, tab, end; };
#ifdef _WIN32
    HANDLE file_=INVALID_HANDLE_VALUE, mapping_=nullptr;
#else
    int file_=-1;
    size_t mapped_size_=0;
#endif
    const char* bytes_=nullptr;
    std::vector<Line> index_;
    std::unordered_map<std::wstring,std::vector<std::wstring>> personal_;
    std::unordered_map<std::wstring,std::vector<std::wstring>> phrases_;
    std::unordered_map<std::wstring,std::vector<std::wstring>> supplements_;
    std::string error_;
public:
    ~Dictionary();
    Dictionary()=default;
    Dictionary(const Dictionary&)=delete;
    Dictionary& operator=(const Dictionary&)=delete;
    bool open(const std::wstring& path);
    void load_personal(const std::wstring& path);
    void load_phrases(const std::wstring& path);
    void load_supplements(const std::wstring& path);
    std::vector<std::wstring> lookup(std::wstring_view word) const;
    size_t size() const { return index_.size(); }
    size_t index_bytes() const { return index_.capacity()*sizeof(Line); }
    const std::string& error() const { return error_; }
};
}
