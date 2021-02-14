#pragma once

#include "agen.hpp"
#include "future.hpp"

class RangeGenerator : public AGenerator<int> {
	int s, e;
	int c;
	public:
		RangeGenerator(int start, int end) : s(start), e(end), c(s-1) {}
		bool done() const;
		std::variant<std::shared_ptr<FutureBase>, something<int>> resume(const Yengine* eng);
};

#include <thread>
#include "engine.hpp"

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
		std::optional<something<T>> r;
		std::optional<something<T>> result(){ return r; }
};

template<typename T> std::shared_ptr<Future<T>> asyncSleep(Yengine* engine, unsigned ms, T ret){
	std::shared_ptr<OutsideFuture<T>> f(new OutsideFuture<T>());
	std::thread th([engine, ms](std::shared_ptr<OutsideFuture<T>> f, something<T> rt){
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
		f->s = FutureState::Completed;
		f->r.emplace(rt);
		engine->notify(std::dynamic_pointer_cast<Future<T>>(f));
	}, f, something<T>(ret));
	th.detach();
	return f;
}
