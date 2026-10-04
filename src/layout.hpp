#pragma once
#include <windows.h>
#include "options.hpp"
namespace ea {
struct OptionCell {RECT bounds{};int index=0;};
struct OptionGroup {RECT bounds{};std::wstring word;};
struct FlowLayout {std::vector<OptionGroup> groups;std::vector<OptionCell> cells;int height=0;};
FlowLayout grouped_flow(const std::vector<EnglishOption>& options,const std::vector<int>& text_widths,int first,int count,int width,int dpi);
struct PopupLayout {RECT bounds{};int capacity=0,page=0;bool above=true;FlowLayout flow;};
PopupLayout popup_layout(RECT candidates,RECT work,int dpi,const std::vector<EnglishOption>& options,const std::vector<int>& text_widths,int page,bool has_status,int available_width=0);
}
