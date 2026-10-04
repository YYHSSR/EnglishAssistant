#pragma once
#include "neural.hpp"
#include <functional>
#include <optional>
namespace ea {
// One latest request; inference never blocks IME reading or English selection.
class SentenceEngine {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    SentenceEngine(std::wstring root,std::function<void()> updated);
    ~SentenceEngine();
    void select(const std::wstring& text);
    std::optional<NeuralReply> lookup(const std::wstring& text);
};
}
