#include "offline.hpp"
#include <algorithm>
#include <cwctype>
#include <fstream>
namespace ea {
bool valid_english_output(std::wstring_view text){return !text.empty()&&text.find(L"[untranslated:")==std::wstring_view::npos&&text.find(L"〔未收录：")==std::wstring_view::npos;}
std::wstring OfflineTranslator::normalize(std::wstring_view text){
    std::wstring result;bool space=false;
    for(wchar_t c:text){if(iswspace(c)){space=!result.empty();continue;}if(space){result+=L' ';space=false;}result+=c>=L'A'&&c<=L'Z'?c-L'A'+L'a':c;}
    while(!result.empty()&&(result.back()==L'.'||result.back()==L'?'||result.back()==L'!'||result.back()==L'。'||result.back()==L'？'||result.back()==L'！'))result.pop_back();
    while(!result.empty()&&result.back()==L' ')result.pop_back();
    return result;
}
void OfflineTranslator::add_reverse_phrases(const fs::path& path){
    std::ifstream in{path};std::string line;
    while(std::getline(in,line)){
        if(line.size()>=3&&line.substr(0,3)=="\xef\xbb\xbf")line.erase(0,3);
        if(line.empty()||line[0]=='#')continue;
        auto tab=line.find('\t');if(tab==std::string::npos)continue;
        auto chinese=wide(std::string_view(line).substr(0,tab));
        for(const auto&english:parse_senses(std::string_view(line).substr(tab+1)))reverse_phrases_[normalize(english)]=chinese;
    }
}
bool OfflineTranslator::open(const std::wstring& folder,bool use_personal){
    patterns_.clear();reverse_phrases_.clear();
    auto root=native_path(folder);
    auto path=[&](const char* file){return wide((root/file).u8string());};
    if(!forward_.open(path("data/glossary-en.tsv"))||!reverse_.open(path("data/glossary-zh.tsv")))return false;
    forward_.load_phrases(path("data/phrases.tsv"));forward_.load_supplements(path("data/supplements.tsv"));
    add_reverse_phrases(root/"data/phrases.tsv");
    if(use_personal){forward_.load_personal(path("personal.tsv"));add_reverse_phrases(root/"personal.tsv");}
    std::ifstream in{root/"data/chat-patterns.tsv"};std::string line;
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
ForwardReply OfflineTranslator::translate_to_english(const std::wstring& text)const{
    ForwardReply reply;
    if(text.empty()||text.size()>8000||std::all_of(text.begin(),text.end(),[](wchar_t c){return iswspace(c);}))return reply;
    reply.senses=to_english(text);if(!reply.senses.empty())return reply;
    auto normalized=normalize(text);reply.senses=to_english(normalized);
    if(!reply.senses.empty())return reply;
    auto chinese=[](wchar_t c){return (c>=0x3400&&c<=0x9fff)||(c>=0xf900&&c<=0xfaff);};
    auto punctuation=[](wchar_t c){return std::wstring_view(L"，,。.！!？?；;：:").find(c)!=std::wstring_view::npos;};
    std::wstring output;
    auto append=[&](const std::wstring& value){if(value.empty())return;if(!output.empty()&&output.back()!=L'\n'&&output.back()!=L' ')output+=L' ';output+=value;};
    for(size_t at=0;at<text.size();){
        if(iswspace(text[at])){if(text[at]==L'\n')output+=L'\n';++at;continue;}
        if(!chinese(text[at])){
            wchar_t c=text[at++];
            if(punctuation(c)){
                output+=c==L'，'?L',':c==L'。'?L'.':c==L'！'?L'!':c==L'？'?L'?':c==L'；'?L';':c==L'：'?L':':c;
            }else{size_t begin=at-1;while(at<text.size()&&!chinese(text[at])&&!iswspace(text[at])&&!punctuation(text[at]))++at;append(text.substr(begin,at-begin));}
            continue;
        }
        size_t end=at;while(end<text.size()&&chinese(text[end]))++end;
        auto clause=text.substr(at,end-at);auto exact=to_english(clause);
        if(!exact.empty()){append(exact[0]);at=end;continue;}
        // Dynamic programming prefers full word coverage, then longer known
        // terms. This is explicitly a dictionary reference, not an MT model.
        size_t n=clause.size();std::vector<int> cost(n+1,1000000);cost[n]=0;
        struct Part{size_t length=1;std::wstring translation;bool unknown=true;};
        std::vector<Part> parts(n);
        for(size_t i=n;i-->0;){
            cost[i]=50+cost[i+1];parts[i]={1,clause.substr(i,1),true};
            for(size_t length=1;length<=std::min(size_t(32),n-i);++length){
                auto senses=forward_.lookup(std::wstring_view(clause).substr(i,length));
                if(senses.empty())continue;
                int value=(length==1?5:1)+cost[i+length];
                if(value<cost[i]){cost[i]=value;parts[i]={length,senses[0],false};}
            }
        }
        for(size_t i=0;i<n;){
            if(parts[i].unknown){size_t begin=i;while(i<n&&parts[i].unknown)++i;reply.unknown.push_back(clause.substr(begin,i-begin));}
            else{append(parts[i].translation);i+=parts[i].length;}
        }
        reply.reference=true;at=end;
    }
    // Preserve diagnostics, but never expose a sentence with missing content.
    if(!output.empty()&&reply.unknown.empty())reply.senses={std::move(output)};
    return reply;
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
