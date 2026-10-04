#pragma once
#include "dictionary.hpp"
#include <filesystem>
#include <functional>
namespace ea {
struct OfflineReply {std::wstring text;bool exact=false;std::vector<std::wstring> unknown;};
class OfflineTranslator {
    Dictionary forward_,reverse_;
    struct Pattern {std::wstring chinese,english;};
    std::vector<Pattern> patterns_;
    std::unordered_map<std::wstring,std::wstring> reverse_phrases_;
    static std::wstring normalize(std::wstring_view text);
    void add_reverse_phrases(const std::filesystem::path& path);
    std::wstring chinese_entry(const std::wstring& text)const;
public:
    bool open(const std::wstring& folder);
    std::vector<std::wstring> to_english(const std::wstring& text)const;
    OfflineReply to_chinese(const std::wstring& text)const;
    size_t english_size()const{return forward_.size();}
    size_t chinese_size()const{return reverse_.size();}
};
}
