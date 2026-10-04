#include "layout.hpp"
#include "options.hpp"
#include <algorithm>
namespace ea {
PopupLayout popup_layout(RECT candidates,RECT work,int dpi,int options,int page,bool has_status){
    PopupLayout result;
    auto px=[&](int value){return MulDiv(value,std::max(dpi,96),96);};
    int safe_top=work.top+px(6),safe_bottom=work.bottom-px(6);
    int safe_left=work.left+px(6),safe_right=work.right-px(6);
    int width=std::min(px(420),safe_right-safe_left);
    if(width<=0||safe_bottom<=safe_top)return result;
    // The accessible menu excludes the preedit/pinyin line above it. Reserve
    // 32 logical pixels for that line, plus a 12-pixel clear gap.
    int above_anchor=std::min((int)candidates.top-px(44),safe_bottom);
    int below_anchor=std::max((int)candidates.bottom+px(12),safe_top);
    int above_space=above_anchor-safe_top,below_space=safe_bottom-below_anchor;
    int minimum=px(42+(options>0?50:0)+((has_status||options>1)?30:8));
    result.above=above_space>=minimum || (below_space<minimum&&above_space>=below_space);
    int space=result.above?above_space:below_space;
    int height=0;
    for(int capacity=page_size;capacity>=1;--capacity){
        int pages=std::max(1,(options+capacity-1)/capacity);
        int current=std::clamp(page,0,pages-1);
        int rows=std::max(0,std::min(capacity,options-current*capacity));
        // Size against a full page, so a shorter last page cannot change capacity.
        int required=px(42+50*std::min(capacity,options)+((has_status||pages>1)?30:8));
        if(required>space)continue;
        result.capacity=capacity;result.page=current;
        height=px(42+50*rows+((has_status||pages>1)?30:8));break;
    }
    if(!result.capacity)return result;
    int left=std::clamp((int)candidates.left,safe_left,safe_right-width);
    int top=result.above?above_anchor-height:below_anchor;
    result.bounds={left,top,left+width,top+height};return result;
}
}
