#include "engine.hpp"

#include "daemons.hpp"

namespace yasync {

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

void Yengine::execute(const AGenf& gf){
	gf->setState(FutureState::Queued);
	work.push(gf);
}
void Yengine::notify(const ANotf& nf){
	work.push(nf);
}

void Yengine::notifiAdd(AFuture k, AGenf v){
	std::unique_lock lock(notificationsLock);
	notifications[k] = v;
}
std::optional<AGenf> Yengine::notifiDrop(AFuture k){
	std::unique_lock lock(notificationsLock);
	auto naut = notifications.find(k);
	if(naut == notifications.end()) return std::nullopt;
	auto ret = naut->second;
	notifications.erase(naut);
	if(notifications.empty()) condWLE.notify_all();
	return ret;
}

void Yengine::threado(AFuture task){
	if(task.state() == FutureState::Cancelled){ //TODO let generators manage cancellations actually. But already it requires fixing a lot of them, so implement default behaviour for now
		while(auto naut = notifiDrop(task)) task = *naut; //which is nuking the await chain. COOOL
		return;
	} else if(task.state() == FutureState::Completed){ //completed outside futures (via notify)
		if(auto naut = notifiDrop(task)) task = *naut; //chained future contains backref to the outside task and will steal the result itself like a good one
		else return; //nothing to chain outside future with
	} else if(task.state() > FutureState::Running) return; //Only suspended tasks are resumeable
	task.visit(overloaded {
		[this](const ANotf&){
			//No-op. Completed case handled above. At best a warning could be emitted idk.
		},
		[this](AGenf task){
			while(true){
				task->setState(FutureState::Running);
				if(auto awa = task->resume(this)) switch(awa->state()){
					case FutureState::Cancelled:
					case FutureState::Completed:
						break;
					case FutureState::Suspended:
						task->setState(FutureState::Awaiting);
						notifiAdd(*awa, task);
						if(auto gawa = awa->genf()) task = *gawa;
						else return;
						break;
					case FutureState::Queued: //This is stoopid, but hey we don't want to sync what we don't need, so it'll wait
					case FutureState::Awaiting:
					case FutureState::Running:
						task->setState(FutureState::Awaiting);
						notifiAdd(*awa, task);
						return;
				} else {
					task->setState(task->done() ? FutureState::Completed : FutureState::Suspended);
					//#BeLazy: Whether we're done or not, drop from notifications. If we're done, well that's it. If we aren't, someone up in the pipeline will await for us at some point, setting up the notifications once again.
					if(auto naut = notifiDrop(AFuture(task))) task = *naut; //Proceed up the await chain immediately
					else return;
				}
			}
		},
	});
}
void Yengine::threadwork(){
	while(auto w = work.pop()) threado(*w);
}

}
