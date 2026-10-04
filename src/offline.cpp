#include "offline.hpp"
#include <algorithm>
#include <cwctype>
#include <fstream>
namespace ea {
std::wstring OfflineTranslator::normalize(std::wstring_view text){
    std::wstring result;bool space=false;
    for(wchar_t c:text){if(iswspace(c)){space=!result.empty();continue;}if(space){result+=L' ';space=false;}result+=c>=L'A'&&c<=L'Z'?c-L'A'+L'a':c;}
    while(!result.empty()&&(result.back()==L'.'||result.back()==L'?'||result.back()==L'!'||result.back()==L'。'||result.back()==L'？'||result.back()==L'！'))result.pop_back();
    while(!result.empty()&&result.back()==L' ')result.pop_back();
    return result;
}
void OfflineTranslator::add_reverse_phrases(const std::filesystem::path& path){
    std::ifstream in{path};std::string line;
    while(std::getline(in,line)){
        if(line.size()>=3&&line.substr(0,3)=="\xef\xbb\xbf")line.erase(0,3);
        if(line.empty()||line[0]=='#')continue;
        auto tab=line.find('\t');if(tab==std::string::npos)continue;
        auto chinese=wide(std::string_view(line).substr(0,tab));
        for(const auto&english:parse_senses(std::string_view(line).substr(tab+1)))reverse_phrases_[normalize(english)]=chinese;
    }
}
bool OfflineTranslator::open(const std::wstring& folder){
    auto root=std::filesystem::path(folder);
    if(!forward_.open((root/L"data/glossary-en.tsv").wstring())||!reverse_.open((root/L"data/glossary-zh.tsv").wstring()))return false;
    forward_.load_phrases((root/L"data/phrases.tsv").wstring());forward_.load_supplements((root/L"data/supplements.tsv").wstring());forward_.load_personal((root/L"personal.tsv").wstring());
    add_reverse_phrases(root/L"data/phrases.tsv");add_reverse_phrases(root/L"personal.tsv");
    std::ifstream in{root/L"data/chat-patterns.tsv"};std::string line;
    while(std::getline(in,line)){
        if(line.empty()||line[0]=='#')continue;
        auto tab=line.find('\t');if(tab==std::string::npos)continue;
        auto zh=wide(std::string_view(line).substr(0,tab)),en=wide(std::string_view(line).substr(tab+1));if(!en.empty()&&en.back()==L'\r')en.pop_back();
        if(zh.find(L"{x}")!=std::wstring::npos&&en.find(L"{x}")!=std::wstring::npos)patterns_.push_back({std::move(zh),std::move(en)});
    }
    return true;
}
static std::wstring slot(const std::wstring& text,const std::wstring& pattern){
    auto at=pattern.find(L"{x}");if(at==std::wstring::npos)return {};
    auto prefix=pattern.substr(0,at),suffix=pattern.substr(at+3);
    if(text.size()<=prefix.size()+suffix.size()||text.compare(0,prefix.size(),prefix)!=0||text.compare(text.size()-suffix.size(),suffix.size(),suffix)!=0)return {};
    return text.substr(prefix.size(),text.size()-prefix.size()-suffix.size());
}
static std::wstring fill(std::wstring pattern,const std::wstring& value){pattern.replace(pattern.find(L"{x}"),3,value);return pattern;}
std::vector<std::wstring> OfflineTranslator::to_english(const std::wstring& text)const{
    auto exact=forward_.lookup(text);if(!exact.empty())return exact;
    for(const auto&pattern:patterns_){auto value=slot(text,pattern.chinese);if(value.empty())continue;auto translated=forward_.lookup(value);if(!translated.empty())return {fill(pattern.english,translated[0])};}
    return {};
}
std::wstring OfflineTranslator::chinese_entry(const std::wstring& text)const{
    auto key=normalize(text);auto phrase=reverse_phrases_.find(key);if(phrase!=reverse_phrases_.end())return phrase->second;
    auto senses=reverse_.lookup(key);std::wstring result;
    for(const auto&sense:senses){if(!result.empty())result+=L"；";result+=sense;}
    return result;
}
OfflineReply OfflineTranslator::to_chinese(const std::wstring& text)const{
    OfflineReply reply;auto normalized=normalize(text);if(normalized.empty())return reply;
    auto exact=chinese_entry(normalized);if(!exact.empty()){reply.text=std::move(exact);reply.exact=true;return reply;}
    for(const auto&pattern:patterns_){auto value=slot(normalized,normalize(pattern.english));if(value.empty())continue;auto translated=chinese_entry(value);if(!translated.empty()){reply.text=fill(pattern.chinese,translated.substr(0,translated.find(L"；")));reply.exact=true;return reply;}}
    // Greedy phrase lookup is reference material, not a sentence translator.
    // Keep punctuation/newlines, match complete word boundaries and mark gaps.
    auto word_char=[](wchar_t c){return (c>=L'A'&&c<=L'Z')||(c>=L'a'&&c<=L'z')||(c>=L'0'&&c<=L'9')||c==L'\''||c==L'+'||c==L'#'||c==L'_';};
    for(size_t at=0;at<text.size();){
        if(iswspace(text[at])){if(text[at]==L'\n')reply.text+=L"\r\n";++at;continue;}
        if(!word_char(text[at])){wchar_t c=text[at++];reply.text+=c==L','?L'，':c==L'.'?L'。':c==L'?'?L'？':c==L'!'?L'！':c;continue;}
        size_t end=at,best_end=at;std::wstring best;
        for(int words=0;words<8;++words){
            while(end<text.size()&&word_char(text[end]))++end;
            auto translation=chinese_entry(text.substr(at,end-at));if(!translation.empty()){best=std::move(translation);best_end=end;}
            size_t next=end;while(next<text.size()&&(text[next]==L' '||text[next]==L'\t'))++next;
            if(next==end||next==text.size()||!word_char(text[next]))break;
            end=next;
        }
        if(!best.empty()){if(!reply.text.empty()&&reply.text.back()!=L'\n')reply.text+=L" / ";reply.text+=best;at=best_end;}
        else{end=at;while(end<text.size()&&word_char(text[end]))++end;auto unknown=text.substr(at,end-at);reply.unknown.push_back(unknown);reply.text+=L"〔未收录："+unknown+L"〕";at=end;}
    }
    return reply;
}
}
