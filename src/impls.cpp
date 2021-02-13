#include "impls.hpp"

#include <thread>

#include "engine.hpp"

bool RangeGenerator::done() const { return c >= e; }
std::variant<std::shared_ptr<FutureBase>, int> RangeGenerator::resume([[maybe_unused]] const Yengine* eng){
	return c++;
}

/*class UnFuture : public Future<void> {
	public:
		FutureState s = FutureState::Running;
		FutureState state(){ return s; }
		std::optional<void> result(){
			return std::optional(void);
		}
};*/

template<typename T> class OutsideFuture : public Future<T> {
	public:
		OutsideFuture(){}
		FutureState s = FutureState::Running;
		FutureState state(){ return s; }
		std::optional<T> r;
		std::optional<T> result(){ return r; }
};

template<typename T> std::shared_ptr<Future<T>> asyncSleep(Yengine* engine, unsigned ms, T ret){
	std::shared_ptr<OutsideFuture<T>> f(new OutsideFuture<T>());
	std::thread th([engine, ms, ret](std::shared_ptr<OutsideFuture<T>> f){
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
		f->s = FutureState::Completed;
		f->r = ret;
		engine->notify(f);
	}, f);
	th.detach();
	return f;
}
