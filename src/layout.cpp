#include "layout.hpp"
#include "options.hpp"
#include <algorithm>
namespace ea {
FlowLayout grouped_flow(const std::vector<EnglishOption>& options,const std::vector<int>& text_widths,int first,int count,int width,int dpi){
    FlowLayout flow;
    auto px=[&](int value){return MulDiv(value,std::max(dpi,96),96);};
    int top=px(42),left=px(18),right=width-px(18);
    if(right<=left)return flow;
    first=std::clamp(first,0,(int)options.size());
    int end=first+std::clamp(count,0,(int)options.size()-first);
    for(int begin=first;begin<end;){
        int next=begin+1;
        while(next<end&&options[next].candidate==options[begin].candidate)++next;
        int x=left,y=top+px(24);
        for(int index=begin;index<next;++index){
            int text=index<(int)text_widths.size()?text_widths[index]:0;
            int cell_width=std::clamp(text+px(40),std::min(px(64),right-left),right-left);
            if(x>left&&x+cell_width>right){x=left;y+=px(34);}
            flow.cells.push_back({{x,y,x+cell_width,y+px(30)},index});
            x+=cell_width+px(6);
        }
        int bottom=y+px(36);
        flow.groups.push_back({{px(10),top,width-px(10),bottom},options[begin].word+(options[begin].reference?L" · 词组参考":L"")});
        top=bottom+px(6);begin=next;
    }
    flow.height=flow.groups.empty()?px(42):top-px(6);
    return flow;
}
PopupLayout popup_layout(RECT candidates,RECT work,int dpi,const std::vector<EnglishOption>& options,const std::vector<int>& text_widths,int page,bool has_status,int available_width){
    PopupLayout result;
    auto px=[&](int value){return MulDiv(value,std::max(dpi,96),96);};
    int safe_top=work.top+px(6),safe_bottom=work.bottom-px(6);
    int safe_left=work.left+px(6),safe_right=work.right-px(6);
    int preferred=std::clamp((int)(candidates.right-candidates.left),px(420),px(560));
    if(available_width>0)preferred=std::min(preferred,std::max(px(240),available_width-px(12)));
    int width=std::min(preferred,safe_right-safe_left);
    if(width<=px(80)||safe_bottom<=safe_top)return result;
    // The accessible menu excludes the preedit/pinyin line above it. Reserve
    // 32 logical pixels for that line, plus a 12-pixel clear gap.
    int above_anchor=std::min((int)candidates.top-px(44),safe_bottom);
    int below_anchor=std::max((int)candidates.bottom+px(12),safe_top);
    int above_space=above_anchor-safe_top,below_space=safe_bottom-below_anchor;
    int total=(int)options.size();
    int minimum=grouped_flow(options,text_widths,0,std::min(total,1),width,dpi).height+px((has_status||total>1)?30:8);
    result.above=above_space>=minimum || (below_space<minimum&&above_space>=below_space);
    int space=result.above?above_space:below_space;
    int height=0;
    for(int capacity=page_size;capacity>=1;--capacity){
        int pages=std::max(1,(total+capacity-1)/capacity);
        int current=std::clamp(page,0,pages-1);
        // Every page must fit at this capacity. A shorter last page therefore
        // cannot change numbering, and grouped wrapping cannot hide any option.
        int required=0;
        for(int p=0;p<pages;++p)
            required=std::max(required,grouped_flow(options,text_widths,p*capacity,capacity,width,dpi).height+px((has_status||pages>1)?30:8));
        if(required>space)continue;
        result.capacity=capacity;result.page=current;
        result.flow=grouped_flow(options,text_widths,current*capacity,capacity,width,dpi);
        height=result.flow.height+px((has_status||pages>1)?30:8);break;
    }
    if(!result.capacity)return result;
    int left=std::clamp((int)candidates.left,safe_left,safe_right-width);
    int top=result.above?above_anchor-height:below_anchor;
    result.bounds={left,top,left+width,top+height};return result;
}
}
