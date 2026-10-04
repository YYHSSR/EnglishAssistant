#include "options.hpp"
#include <algorithm>
namespace ea {
std::vector<EnglishOption> english_options(const Snapshot& s){
    std::vector<EnglishOption> options;
    for(const auto& candidate:s.candidates)
        for(size_t sense=0;sense<candidate.senses.size();++sense)
            options.push_back({candidate.number,(int)sense,candidate.word,candidate.senses[sense]});
    return options;
}
int move_selection(int current,int direction,int total,int first_visible){
    if(total<=0)return -1;
    if(current<0||current>=total)return std::clamp(first_visible,0,total-1);
    return std::clamp(current+(direction<0?-1:1),0,total-1);
}
}
