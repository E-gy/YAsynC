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

void Yengine::notify(AFuture f){ work.push(f); }

void Yengine::wle(){
	{
		std::unique_lock lock(notificationsLock);
		while(work.currentIdle() < work.thresIdle || !notifications.empty()) condWLE.wait(lock);
	}
	work.close();
}

void Yengine::notifiAdd(AFuture k, AFuture v){
	std::unique_lock lock(notificationsLock);
	notifications[k] = v;
}
std::optional<AFuture> Yengine::notifiDrop(AFuture k){
	std::unique_lock lock(notificationsLock);
	auto naut = notifications.find(k);
	if(naut == notifications.end()) return std::nullopt;
	auto ret = naut->second;
	notifications.erase(naut);
	if(notifications.empty()) condWLE.notify_all();
	return ret;
}

void Yengine::threado(AFuture task){ 
	if(task->state() == FutureState::Cancelled){ //TODO let generators manage cancellations actually. But already it requires fixing a lot of them, so implement default behaviour for now
		while(auto naut = notifiDrop(task)) task = *naut; //which is nuking the await chain. COOOL
		return;
	} if(task->state() == FutureState::Completed){ //completed outside futures (via notify)
		if(auto naut = notifiDrop(task)) task = *naut; //chained future contains backref to the outside task and will steal the result itself like a good one
		else return; //nothing to chain outside future with
	} else if(task->state() > FutureState::Running) return; //Only suspended tasks are resumeable
	//cont:
	while(true)
	{
	#ifdef _DEBUG
	if(task->isExternal()){
		std::cout << "!CRITICAL! external task got inside work loop " << task.get() << "\n" << task->trace;
		std::cout << "skipping...\n";
		return;
	}
	#endif
	auto gent = reinterpret_cast<IFutureG<void*>*>(task.get());
	gent->set(FutureState::Running);
	auto g = gent->gen->resume(this);
	if(auto awa = std::get_if<AFuture>(&g)){
		switch((*awa)->state()){
			case FutureState::Cancelled:
			case FutureState::Completed:
				// goto cont;
				break;
			case FutureState::Suspended:
				gent->set(FutureState::Awaiting);
				notifiAdd(*awa, task);
				task = *awa;
				// goto cont;
				break;
			case FutureState::Queued: //This is stoopid, but hey we don't want to sync what we don't need, so it'll wait
			case FutureState::Awaiting:
			case FutureState::Running:
				gent->set(FutureState::Awaiting);
				notifiAdd(*awa, task);
				return;
		}
	} else {
		gent->set(gent->gen->done() ? FutureState::Completed : FutureState::Suspended, std::move(std::get<movonly<void*>>(g))); //do NOT!!! copy. C++ compiler reaaally wants to copy. NO!
		//#BeLazy: Whether we're done or not, drop from notifications. If we're done, well that's it. If we aren't, someone up in the pipeline will await for us at some point, setting up the notifications once again.
		if(auto naut = notifiDrop(task)) task = *naut; //Proceed up the await chain immediately
		else return;
	}
	}
}
void Yengine::threadwork(){
	while(auto w = work.pop()) threado(*w);
}

}
