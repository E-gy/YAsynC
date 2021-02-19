#pragma once

#include "result.hpp"
#include "engine.hpp"

namespace yasync::io {

[[deprecated]]
result<Future<void>, std::string> onCtrlC(Yengine*);
[[deprecated]]
void unCtrlC();

result<void, std::string> mainThreadWaitCtrlC();

}
