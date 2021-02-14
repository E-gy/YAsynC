#include "engine.hpp"

#include <thread>

Yengine::Yengine(unsigned threads) : workers(threads) {
	work.cvIdle = &condWLE;
	work.thresIdle = workers;
	for(unsigned i = 0; i < workers; i++){
		std::thread th([this](){ this->threadwork(); });
		th.detach();
	}
}

void Yengine::wle(){
	{
		std::unique_lock lock(notificationsLock);
		while(work.currentIdle() < work.thresIdle || !notifications.empty()) condWLE.wait(lock);
	}
	work.close();
}
