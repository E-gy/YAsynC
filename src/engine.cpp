#include "engine.hpp"

#include <thread>

Yengine::Yengine(unsigned threads){
	for(unsigned i = 0; i < threads; i++){
		std::thread th([this](){ this->threadwork(); });
		th.detach();
	}
}
