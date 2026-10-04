#include "dictionary.hpp"
#include <algorithm>
#include <fstream>
#include <filesystem>
#ifndef _WIN32
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ea {
std::wstring wide(std::string_view s) {
#ifdef _WIN32
    if(s.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);
    if(n<=0)return {};
    std::wstring r(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),r.data(),n);return r;
#else
    std::wstring result;
    for(size_t i=0;i<s.size();){
        uint32_t c=(unsigned char)s[i++];int following=0;uint32_t minimum=0;
        if(c>=0xc2&&c<=0xdf){following=1;minimum=0x80;c&=31;}
        else if(c>=0xe0&&c<=0xef){following=2;minimum=0x800;c&=15;}
        else if(c>=0xf0&&c<=0xf4){following=3;minimum=0x10000;c&=7;}
        else if(c>=0x80)return {};
        if(i+following>s.size())return {};
        while(following--){auto b=(unsigned char)s[i++];if((b&0xc0)!=0x80)return {};c=(c<<6)|(b&63);}
        if(c<minimum||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return {};
        result+=static_cast<wchar_t>(c);
    }
    return result;
#endif
}
std::string utf8(std::wstring_view s) {
#ifdef _WIN32
    if(s.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    if(n<=0)return {};
    std::string r(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),(int)s.size(),r.data(),n,nullptr,nullptr);return r;
#else
    std::string result;
    for(wchar_t value:s){uint32_t c=value;
        if(c>0x10ffff||(c>=0xd800&&c<=0xdfff))return {};
        if(c<0x80)result+=char(c);
        else if(c<0x800){result+=char(0xc0|(c>>6));result+=char(0x80|(c&63));}
        else if(c<0x10000){result+=char(0xe0|(c>>12));result+=char(0x80|((c>>6)&63));result+=char(0x80|(c&63));}
        else{result+=char(0xf0|(c>>18));result+=char(0x80|((c>>12)&63));result+=char(0x80|((c>>6)&63));result+=char(0x80|(c&63));}
    }
    return result;
#endif
}
std::filesystem::path native_path(std::wstring_view text){
#ifdef _WIN32
    return std::filesystem::path(text);
#else
    return std::filesystem::u8path(utf8(text));
#endif
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
#ifdef _WIN32
    if(bytes_)UnmapViewOfFile(bytes_);
    if(mapping_)CloseHandle(mapping_);
    if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_);
#else
    if(bytes_)munmap(const_cast<char*>(bytes_),mapped_size_);
    if(file_>=0)::close(file_);
#endif
}
bool Dictionary::open(const std::wstring& path){
#ifdef _WIN32
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
#else
    if(file_>=0){error_="Dictionary already open";return false;}
    file_=::open(utf8(path).c_str(),O_RDONLY|O_CLOEXEC);
    if(file_<0){error_="Cannot open glossary";return false;}
    struct stat info{};
    if(fstat(file_,&info)!=0||info.st_size<=0||info.st_size>128*1024*1024){error_="Invalid glossary size";return false;}
    mapped_size_=static_cast<size_t>(info.st_size);
    auto mapped=mmap(nullptr,mapped_size_,PROT_READ,MAP_PRIVATE,file_,0);
    if(mapped==MAP_FAILED){error_="Cannot map glossary";return false;}
    bytes_=static_cast<const char*>(mapped);uint32_t len=static_cast<uint32_t>(mapped_size_);
#endif
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
