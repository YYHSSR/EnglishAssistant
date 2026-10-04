#pragma once
#include "offline.hpp"
#include <memory>
namespace ea {
using DictionaryProvider=std::function<std::shared_ptr<OfflineTranslator>()>;
HWND show_translation_box(HINSTANCE instance,HWND owner,DictionaryProvider provider);
void close_translation_box();
}
