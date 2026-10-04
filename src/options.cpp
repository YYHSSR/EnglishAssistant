#include "options.hpp"
namespace ea {
std::vector<EnglishOption> english_options(const Snapshot& s){
    std::vector<EnglishOption> options;
    for(const auto& candidate:s.candidates)
        for(size_t sense=0;sense<candidate.senses.size();++sense)
            options.push_back({candidate.number,(int)sense,candidate.word,candidate.senses[sense]});
    return options;
}
}
