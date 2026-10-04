#include "offline.hpp"
#include <iostream>
#include <string>
namespace {
std::string json(std::wstring_view value){
    auto s=ea::utf8(value);std::string out="\"";
    const char* digits="0123456789abcdef";
    for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=digits[c>>4];out+=digits[c&15];}else out+=char(c);}
    return out+'"';
}
std::string array(const std::vector<std::wstring>& values){std::string out="[";for(const auto&v:values){if(out.size()>1)out+=',';out+=json(v);}return out+']';}
std::wstring decode(const std::string& hex){
    if(hex.size()%2||hex.size()>64000)return {};
    auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
    std::string result;for(size_t i=0;i<hex.size();i+=2){int a=digit(hex[i]),b=digit(hex[i+1]);if(a<0||b<0)return {};result+=char(a*16+b);}return ea::wide(result);
}
}
int main(int argc,char**argv){
    if(argc!=3||std::string(argv[1])!="--serve"){std::cerr<<"Usage: EnglishAssistantCore --serve PROJECT_DIRECTORY\n";return 2;}
    ea::OfflineTranslator translator;if(!translator.open(ea::wide(argv[2]))){std::cerr<<"Cannot open bilingual dictionaries\n";return 1;}
    std::cout<<"{\"ready\":true,\"english\":"<<translator.english_size()<<",\"chinese\":"<<translator.chinese_size()<<"}"<<std::endl;
    std::string line;
    while(std::getline(std::cin,line)){
        auto tab=line.find('\t');auto command=line.substr(0,tab);
        auto text=tab==std::string::npos?std::wstring{}:decode(line.substr(tab+1));
        if(command=="en"){auto r=translator.translate_to_english(text);std::cout<<"{\"senses\":"<<array(r.senses)<<",\"reference\":"<<(r.reference?"true":"false")<<",\"unknown\":"<<array(r.unknown)<<"}"<<std::endl;}
        else if(command=="zh"){auto r=translator.to_chinese(text);std::cout<<"{\"text\":"<<json(r.text)<<",\"exact\":"<<(r.exact?"true":"false")<<",\"unknown\":"<<array(r.unknown)<<"}"<<std::endl;}
        else std::cout<<"{\"error\":\"Unknown command\"}"<<std::endl;
    }
}
