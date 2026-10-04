#pragma once
#include <windows.h>
#include <functional>
#include <string>
namespace ea {
bool read_document(const std::wstring& path,std::wstring& text);
bool save_document(const std::wstring& path,const std::wstring& text);
HWND show_document(HINSTANCE instance,HWND owner,const std::wstring& path,
                   const std::wstring& title,bool editable,std::function<void()> saved={});
}
