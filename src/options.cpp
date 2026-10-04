#include "options.hpp"
#include <algorithm>
namespace ea {
std::vector<EnglishOption> english_options(const Snapshot& s){
    std::vector<EnglishOption> options;
    std::vector<const Candidate*> ordered;
    for(const auto&candidate:s.candidates)ordered.push_back(&candidate);
    if(std::any_of(ordered.begin(),ordered.end(),[](const Candidate*c){return c->word.size()>=4&&!c->senses.empty();}))
        std::stable_sort(ordered.begin(),ordered.end(),[](const Candidate*a,const Candidate*b){return a->word.size()>b->word.size();});
    for(const auto*item:ordered){const auto&candidate=*item;
        for(size_t sense=0;sense<candidate.senses.size();++sense)
            options.push_back({candidate.number,(int)sense,candidate.word,candidate.senses[sense]});
    }
    return options;
}
int move_selection(int current,int direction,int total,int first_visible){
    if(total<=0)return -1;
    if(current<0||current>=total)return std::clamp(first_visible,0,total-1);
    return std::clamp(current+(direction<0?-1:1),0,total-1);
}
}
