#pragma once
#include <windows.h>
namespace ea {
struct PopupLayout {RECT bounds{};int capacity=0,page=0;bool above=true;};
PopupLayout popup_layout(RECT candidates,RECT work,int dpi,int options,int page,bool has_status);
}
