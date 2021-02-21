#pragma once

#include "result.hpp"
#include "engine.hpp"

namespace yasync::io {

class CtrlC {
	public:
		static void setup();
		static result<Future<void>, std::string> on(Yengine*);
		static void un(Yengine*);
};

[[deprecated]]
result<void, std::string> mainThreadWaitCtrlC();

}
