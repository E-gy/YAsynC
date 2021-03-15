#include "engine.hpp"

#include "daemons.hpp"

namespace yasync {

#ifdef _DEBUG
bool IFuture::isExternal(){ return true; }
#endif

std::ostream& operator<<(std::ostream& os, const FutureState& state){
	switch(state){
		case FutureState::Suspended: return os << "Suspended";
		case FutureState::Queued: return os << "Queued";
		case FutureState::Running: return os << "Running";
		case FutureState::Awaiting: return os << "Awaiting";
		case FutureState::Completed: return os << "Completed";
		case FutureState::Cancelled: return os << "Cancelled";
		default: return os << "<Invalid State>";
	}
}


Yengine::Yengine(unsigned threads) : workers(threads) {
	work.cvIdle = &condWLE;
	work.thresIdle = workers;
	for(unsigned i = 0; i < workers; i++) Daemons::launch([this](){ this->threadwork(); });
}

void Yengine::wle(){
	{
		std::unique_lock lock(notificationsLock);
		while(work.currentIdle() < work.thresIdle || !notifications.empty()) condWLE.wait(lock);
	}
	work.close();
}

}
