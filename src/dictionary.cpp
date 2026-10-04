#include "dictionary.hpp"
#include <algorithm>
#include <fstream>

namespace ea {
std::wstring wide(std::string_view s) {
    if(s.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);
    if(n<=0)return {};
    std::wstring r(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),r.data(),n);return r;
}
std::string utf8(std::wstring_view s) {
    if(s.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    if(n<=0)return {};
    std::string r(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),(int)s.size(),r.data(),n,nullptr,nullptr);return r;
}
fs::path native_path(std::wstring_view text){
    return fs::path(text);
}
static std::string_view trim(std::string_view s) {
    while(!s.empty() && (s.front()==' '||s.front()=='\r'))s.remove_prefix(1);
    while(!s.empty() && (s.back()==' '||s.back()=='\r'))s.remove_suffix(1);
    return s;
}
std::vector<std::wstring> parse_senses(std::string_view fields) {
    std::vector<std::wstring> out;
    while(!fields.empty() && out.size()<8) {
        auto tab=fields.find('\t');auto s=trim(fields.substr(0,tab));
        // Only remove known part-of-speech prefixes, never a period in a word.
        for(auto prefix:{"n. ","v. ","adj. ","adv. ","pron. ","prep. ","conj. ","int. ","interj. ","num. ","det. ","art. ","aux. ","phr. ","abbr. ","modal. "}) {
            std::string_view p=prefix;
            if(s.substr(0,p.size())==p){s.remove_prefix(p.size());break;}
        }
        s=trim(s.substr(0,s.find('|')));
        auto w=wide(s);
        if(!w.empty() && w.size()<=320 && w.find_first_of(L"\r\n\t")==std::wstring::npos && std::find(out.begin(),out.end(),w)==out.end())out.push_back(std::move(w));
        if(tab==std::string_view::npos)break;
        fields.remove_prefix(tab+1);
    }
    return out;
}
Dictionary::~Dictionary(){
    if(bytes_)UnmapViewOfFile(bytes_);
    if(mapping_)CloseHandle(mapping_);
    if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_);
}
bool Dictionary::open(const std::wstring& path){
    if(file_!=INVALID_HANDLE_VALUE){error_="Dictionary already open";return false;}
    file_=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file_==INVALID_HANDLE_VALUE){error_="Cannot open glossary-en.tsv";return false;}
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file_,&size)||size.QuadPart<=0||size.QuadPart>128*1024*1024){error_="Invalid glossary size";return false;}
    mapping_=CreateFileMappingW(file_,nullptr,PAGE_READONLY,0,0,nullptr);
    if(!mapping_){error_="Cannot map glossary";return false;}
    bytes_=(const char*)MapViewOfFile(mapping_,FILE_MAP_READ,0,0,0);
    if(!bytes_){error_="Cannot read glossary";return false;}
    uint32_t len=(uint32_t)size.QuadPart;
    index_.reserve(std::count(bytes_,bytes_+len,'\n')+1);
    uint32_t start=(len>=3 && std::string_view(bytes_,3)=="\xef\xbb\xbf")?3:0;
    for(uint32_t at=start;at<=len;++at){
        if(at!=len && bytes_[at]!='\n')continue;
        uint32_t end=at;if(end>start && bytes_[end-1]=='\r')--end;
        if(end>start && bytes_[start]!='#'){
            uint32_t tab=start;while(tab<end && bytes_[tab]!='\t')++tab;
            if(tab>start && tab<end)index_.push_back({start,tab,end});
        }
        start=at+1;
    }
    auto key=[&](const Line& l){return std::string_view(bytes_+l.start,l.tab-l.start);};
    if(!std::is_sorted(index_.begin(),index_.end(),[&](const Line& a,const Line& b){return key(a)<key(b);}))
        std::sort(index_.begin(),index_.end(),[&](const Line& a,const Line& b){return key(a)<key(b);});
    if(index_.empty()){error_="Glossary contains no entries";return false;}
    return true;
}
void Dictionary::load_personal(const std::wstring& path){
    personal_.clear();std::ifstream in{native_path(path)};std::string line;
    while(std::getline(in,line)){
        if(line.size()>=3 && line.substr(0,3)=="\xef\xbb\xbf")line.erase(0,3);
        if(line.empty()||line.front()=='#')continue;
        auto tab=line.find('\t');if(tab==std::string::npos)continue;
        auto word=wide(std::string_view(line).substr(0,tab));auto senses=parse_senses(std::string_view(line).substr(tab+1));
        if(!word.empty()&&!senses.empty())personal_[std::move(word)]=std::move(senses);
    }
}
void Dictionary::load_phrases(const std::wstring& path){
    phrases_.clear();std::ifstream in{native_path(path)};std::string line;
    while(std::getline(in,line)){
        if(line.size()>=3 && line.substr(0,3)=="\xef\xbb\xbf")line.erase(0,3);
        if(line.empty()||line.front()=='#')continue;
        auto tab=line.find('\t');if(tab==std::string::npos)continue;
        auto word=wide(std::string_view(line).substr(0,tab));auto senses=parse_senses(std::string_view(line).substr(tab+1));
        if(!word.empty()&&!senses.empty())phrases_[std::move(word)]=std::move(senses);
    }
}
std::vector<std::wstring> Dictionary::lookup(std::wstring_view word)const{
    std::wstring word_key(word);
    auto personal=personal_.find(word_key);if(personal!=personal_.end())return personal->second;
    auto phrase=phrases_.find(word_key);if(phrase!=phrases_.end())return phrase->second;
    auto key=utf8(word);
    auto it=std::lower_bound(index_.begin(),index_.end(),std::string_view(key),[&](const Line& l,std::string_view k){return std::string_view(bytes_+l.start,l.tab-l.start)<k;});
    std::vector<std::wstring> senses;
    if(it!=index_.end()&&std::string_view(bytes_+it->start,it->tab-it->start)==key)
        senses=parse_senses(std::string_view(bytes_+it->tab+1,it->end-it->tab-1));
    auto extra=supplements_.find(word_key);
    if(extra!=supplements_.end())for(const auto& sense:extra->second){
        if(senses.size()>=8)break;
        if(std::find(senses.begin(),senses.end(),sense)==senses.end())senses.push_back(sense);
    }
    return senses;
}
void Dictionary::load_supplements(const std::wstring& path){
    supplements_.clear();std::ifstream in{native_path(path)};std::string line;
    while(std::getline(in,line)){
        if(line.size()>=3&&line.substr(0,3)=="\xef\xbb\xbf")line.erase(0,3);
        if(line.empty()||line.front()=='#')continue;
        auto tab=line.find('\t');if(tab==std::string::npos)continue;
        auto word=wide(std::string_view(line).substr(0,tab));auto senses=parse_senses(std::string_view(line).substr(tab+1));
        if(!word.empty()&&!senses.empty())supplements_[std::move(word)]=std::move(senses);
    }
}
}
