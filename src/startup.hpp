#pragma once
#include <windows.h>
#include <string>
namespace ea {
inline constexpr wchar_t startup_key[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
std::wstring startup_command(const std::wstring& executable);
bool startup_enabled(const std::wstring& executable,const wchar_t* key=startup_key);
bool set_startup(const std::wstring& executable,bool enabled,const wchar_t* key=startup_key);
bool migrate_legacy_startup(const std::wstring& executable,const wchar_t* key=startup_key);
}
