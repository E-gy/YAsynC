#pragma once

#include "agen.hpp"
#include "future.hpp"

#include <thread>
#include "engine.hpp"

namespace yasync {

class RangeGenerator : public IGeneratorT<int> {
	int s, e;
	int c;
	public:
		RangeGenerator(int start, int end) : s(start), e(end), c(s-1) {}
		bool done() const;
		std::variant<AFuture, something<int>> resume(const Yengine* eng);
};

/*class UnFuture : public Future<void> {
	public:
		FutureState s = FutureState::Running;
		FutureState state(){ return s; }
		std::optional<void> result(){
			return std::optional(void);
		}
};*/

template<typename T> class OutsideFuture : public IFutureT<T> {
	public:
		OutsideFuture(){}
		FutureState s = FutureState::Running;
		FutureState state(){ return s; }
		std::optional<something<T>> r;
		std::optional<something<T>> result(){ return r; }
};

template<typename T> Future<T> completed(T t){
	std::shared_ptr<OutsideFuture<T>> vf(new OutsideFuture<T>());
	vf->s = FutureState::Completed;
	vf->r.emplace(something<T>(std::move(t)));
	return vf;
} 

template<typename T> Future<T> asyncSleep(Yengine* engine, unsigned ms, T ret){
	std::shared_ptr<OutsideFuture<T>> f(new OutsideFuture<T>());
	std::thread th([engine, ms](std::shared_ptr<OutsideFuture<T>> f, something<T> rt){
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
		f->s = FutureState::Completed;
		f->r.emplace(rt);
		engine->notify(std::dynamic_pointer_cast<IFutureT<T>>(f));
	}, f, something<T>(ret));
	th.detach();
	return f;
}
Future<void> asyncSleep(Yengine* engine, unsigned ms);

/**
 * Blocks current thread until completion of a future (for a generating future, until a result is produced) on the engine
 * DO _NOT_ USE FROM INSIDE ASYNC CODE!
 * @param engine engine to block on
 * @param f future to await
 * @returns the result of the future when completed
 */
template<typename T> T blawait(Yengine* engine, Future<T> f){
	std::unique_ptr<T> t;
	std::mutex synch;
	std::condition_variable cvDone;
	engine <<= f >> [&](auto tres){
		std::unique_lock lok(synch);
		t = std::unique_ptr<T>(new T(std::move(tres.get())));
		cvDone.notify_one();
	};
	std::unique_lock lok(synch);
	while(!t.get()) cvDone.wait(lok);
	return *t;
}
template<> void blawait<void>(Yengine* engine, Future<void> f);

}
