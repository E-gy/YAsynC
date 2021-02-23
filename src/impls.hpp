#pragma once

#include "agen.hpp"
#include "future.hpp"

#include "daemons.hpp"
#include "engine.hpp"

namespace yasync {

template<typename Iter, typename Capt> class IteratingGenerator : public IGeneratorT<Iter> {
	Capt capture;
	Iter s, e, c;
	public:
		IteratingGenerator(Capt capt, Iter start, Iter end) : capture(capt), s(start), e(end), c(s) {}
		template<typename F1, typename F2> IteratingGenerator(Capt capt, F1 f1, F2 f2) : capture(capt), s(f1(capture)), e(f2(capture)), c(s) {}
		bool done(){ return c != e; }
		std::variant<AFuture, movonly<Iter>> resume([[maybe_unused]] const Yengine* engine){
			return movonly<Iter>(c++);
		}
};

template<typename Iter> class IteratingGenerator<Iter, void> : public IGeneratorT<Iter> {
	Iter s, e, c;
	public:
		IteratingGenerator(Iter start, Iter end) : s(start), e(end), c(s) {}
		bool done(){ return c != e; }
		std::variant<AFuture, movonly<Iter>> resume([[maybe_unused]] const Yengine* engine){
			return movonly<Iter>(c++);
		}
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
		movonly<T> r;
		movonly<T> result(){ return std::move(r); }
};

template<typename T> Future<T> completed(const T& t){
	std::shared_ptr<OutsideFuture<T>> vf(new OutsideFuture<T>());
	vf->s = FutureState::Completed;
	vf->r = t;
	return vf;
} 

template<typename T> Future<T> asyncSleep(Yengine* engine, unsigned ms, T ret){
	std::shared_ptr<OutsideFuture<T>> f(new OutsideFuture<T>());
	Daemons::launch([engine, ms](std::shared_ptr<OutsideFuture<T>> f, auto rt){
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
		f->s = FutureState::Completed;
		f->r = std::move(rt);
		engine->notify(std::dynamic_pointer_cast<IFutureT<T>>(f));
	}, f, movonly<T>(new T(ret)));
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
		t = std::unique_ptr<T>(new T(std::move(tres)));
		cvDone.notify_one();
	};
	std::unique_lock lok(synch);
	while(!t.get()) cvDone.wait(lok);
	return *t;
}
template<> void blawait<void>(Yengine* engine, Future<void> f);

}
