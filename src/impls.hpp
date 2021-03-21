#pragma once

#include "agen.hpp"
#include "future.hpp"

#include "engine.hpp"

namespace yasync {

template<typename Iter, typename Capt> class IteratingGenerator : public IGeneratorT<Iter> {
	Capt capture;
	Iter s, e, c;
	public:
		IteratingGenerator(Capt capt, Iter start, Iter end) : capture(capt), s(start), e(end), c(s) {}
		template<typename F1, typename F2> IteratingGenerator(Capt capt, F1 f1, F2 f2) : capture(capt), s(f1(capture)), e(f2(capture)), c(s) {}
		bool done() const override { return c != e; }
		Generesume<Iter> resume(const Yengine*) override {
			return monoid<Iter>(c != e ? c++ : c);
		}
};

template<typename Iter> class IteratingGenerator<Iter, void> : public IGeneratorT<Iter> {
	Iter s, e, c;
	public:
		IteratingGenerator(Iter start, Iter end) : s(start), e(end), c(s) {}
		bool done() const override { return c != e; }
		Generesume<Iter> resume(const Yengine*) override {
			return monoid<Iter>(c != e ? c++ : c);
		}
};

template<typename T> class OutsideFuture final : public INotfT<T> {
	FutureState s = FutureState::Running;
	T r;
	public:
		OutsideFuture(){}
		void set(FutureState st){ s = st; }
		void set(FutureState st, T && rr){
			r = std::move(rr);
			set(st);
		}
		void set(FutureState st, monoid<T> && rr){ return set(st, rr.move()); }
		FutureState state() const override { return s; }
		monoid<T> result() override { return std::move(r); }
		/// Marks future as completed with result
		void completed(T && rr){ return set(FutureState::Completed, std::move(rr)); }
		/// Marks future as running, moves previous result
		T running(){
			set(FutureState::Running);
			return std::move(r);
		}
};

template<> class OutsideFuture<void> final : public INotfT<void> {
	FutureState s = FutureState::Running;
	public:
		OutsideFuture(){}
		void set(FutureState st){ s = st; }
		void set(FutureState st, monoid<void> &&){ s = st; }
		FutureState state() const override { return s; }
		monoid<void> result() override { return {}; }
		/// Marks future as completed
		void completed(){ return set(FutureState::Completed); }
		/// Marks future as running
		void running(){
			set(FutureState::Running);
		}
};

template<typename T> Future<T> completed(T && t){
	auto vf = std::make_shared<OutsideFuture<T>>();
	vf->completed(std::move(t));
	return vf;
}

inline Future<void> completed(){
	auto vf = std::make_shared<OutsideFuture<void>>();
	vf->completed();
	return vf;
}

template<typename T> class AggregateFuture : public INotfT<std::vector<T>> {
	unsigned bal = 0;
	std::mutex synch;
	std::vector<T> results;
	public:
		std::weak_ptr<AggregateFuture> slf;
		AggregateFuture() = default;
		FutureState state() const override { return bal > 0 ? FutureState::Running : FutureState::Completed; }
		monoid<std::vector<T>> result() override { return std::move(results); }
		AggregateFuture& add(Yengine* engine, Future<T> f){
			{
				std::unique_lock lok(synch);
				bal++;
			}
			engine <<= f >> [self = slf.lock(), engine](T t){
				std::unique_lock lok(self->synch);
				self->results.add(std::move(t));
				if(--self->bal == 0) engine->notify(self);
			};
			return *this;
		}
};

template<> class AggregateFuture<void> : public INotfT<void> {
	unsigned bal = 0;
	std::mutex synch;
	public:
		std::weak_ptr<AggregateFuture> slf;
		AggregateFuture() = default;
		FutureState state() const override { return bal > 0 ? FutureState::Running : FutureState::Completed; }
		monoid<void> result() override { return {}; }
		AggregateFuture& add(Yengine* engine, Future<void> f){
			{
				std::unique_lock lok(synch);
				bal++;
			}
			engine <<= f >> [self = slf.lock(), engine](){
				std::unique_lock lok(self->synch);
				if(--self->bal == 0) engine->notify(self);
			};
			return *this;
		}
};

template<typename T> std::shared_ptr<AggregateFuture<T>> aggregAll(){
	std::shared_ptr<AggregateFuture<T>> f(new AggregateFuture<T>());
	f->slf = f;
	return f;
}

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
