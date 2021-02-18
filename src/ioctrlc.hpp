#pragma once

#include "result.hpp"
#include "engine.hpp"

namespace yasync::io {

result<Future<void>, std::string> onCtrlC(Yengine*);
void unCtrlC();

}
