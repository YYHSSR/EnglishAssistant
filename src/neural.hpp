#pragma once
#include <string>
#include <atomic>
namespace ea {
struct NeuralReply {std::wstring text,error;};
NeuralReply neural_translate(const std::wstring& root,const std::wstring& text,const std::atomic<bool>& cancelled);
}
