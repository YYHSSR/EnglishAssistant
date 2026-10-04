#include "startup.hpp"
#include <vector>
namespace ea {
std::wstring startup_command(const std::wstring& executable){return L"\""+executable+L"\"";}
bool startup_enabled(const std::wstring& executable,const wchar_t* key){
    DWORD bytes=0;if(RegGetValueW(HKEY_CURRENT_USER,key,L"EnglishAssistant",RRF_RT_REG_SZ,nullptr,nullptr,&bytes)!=ERROR_SUCCESS||bytes>65536)return false;
    std::vector<wchar_t> value(bytes/sizeof(wchar_t)+1);
    if(RegGetValueW(HKEY_CURRENT_USER,key,L"EnglishAssistant",RRF_RT_REG_SZ,nullptr,value.data(),&bytes)!=ERROR_SUCCESS)return false;
    return _wcsicmp(value.data(),startup_command(executable).c_str())==0;
}
bool set_startup(const std::wstring& executable,bool enabled,const wchar_t* key){
    if(!enabled){auto result=RegDeleteKeyValueW(HKEY_CURRENT_USER,key,L"EnglishAssistant");return result==ERROR_SUCCESS||result==ERROR_FILE_NOT_FOUND;}
    auto command=startup_command(executable);
    return RegSetKeyValueW(HKEY_CURRENT_USER,key,L"EnglishAssistant",REG_SZ,command.c_str(),(DWORD)((command.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS;
}
}
