#pragma once
#include <string>
#include <atomic>
#include <memory>
namespace ea {
enum class TranslationDirection {EnglishToChinese,ChineseToEnglish};
struct NeuralReply {std::wstring text,error;};
class NeuralTranslator {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    explicit NeuralTranslator(std::wstring root);
    ~NeuralTranslator();
    NeuralReply translate(const std::wstring& text,const std::atomic<bool>& cancelled,TranslationDirection direction=TranslationDirection::EnglishToChinese);
    void reset();
};
NeuralReply neural_translate(const std::wstring& root,const std::wstring& text,const std::atomic<bool>& cancelled,TranslationDirection direction=TranslationDirection::EnglishToChinese);
}
