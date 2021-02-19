#include "engine.hpp"

#include <thread>

namespace yasync {

std::ostream& operator<<(std::ostream& os, const FutureState& state){
	switch(state){
		case FutureState::Suspended: return os << "Suspended";
		case FutureState::Queued: return os << "Queued";
		case FutureState::Running: return os << "Running";
		case FutureState::Awaiting: return os << "Awaiting";
		case FutureState::Completed: return os << "Completed";
		default: return os << "<Invalid State>";
	}
}


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

}
