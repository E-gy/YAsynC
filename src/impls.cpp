#include "impls.hpp"

namespace yasync {

bool RangeGenerator::done() const { return c >= e-1; }
std::variant<AFuture, movonly<int>> RangeGenerator::resume([[maybe_unused]] const Yengine* eng){
	c++;
	return c;
}

Future<void> asyncSleep(Yengine* engine, unsigned ms){
	std::shared_ptr<OutsideFuture<void>> f(new OutsideFuture<void>());
	std::thread th([engine, ms](std::shared_ptr<OutsideFuture<void>> f){
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
		f->s = FutureState::Completed;
		engine->notify(std::dynamic_pointer_cast<IFutureT<void>>(f));
	}, f);
	th.detach();
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
