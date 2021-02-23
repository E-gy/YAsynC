#include "impls.hpp"

namespace yasync {

Future<void> asyncSleep(Yengine* engine, unsigned ms){
	std::shared_ptr<OutsideFuture<void>> f(new OutsideFuture<void>());
	Daemons::launch([engine, ms](std::shared_ptr<OutsideFuture<void>> f){
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
		f->s = FutureState::Completed;
		engine->notify(std::dynamic_pointer_cast<IFutureT<void>>(f));
	}, f);
	return f;
}

template<> void blawait<void>(Yengine* engine, Future<void> f){
	bool t = false;
	std::mutex synch;
	std::condition_variable cvDone;
	engine <<= f >> [&](){
		std::unique_lock lok(synch);
		t = true;
		cvDone.notify_one();
	};
	std::unique_lock lok(synch);
	while(!t) cvDone.wait(lok);
}

}
