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
		bool d = false;
		something<T> r;
		OutsideFuture(){}
		FutureState state(){ return d ? FutureState::Completed : FutureState::Running; }
		std::optional<something<T>*> result(){ return d ? std::optional(&r) : std::nullopt; }
		something<T>&& taker(){ return std::move(r); }
		void setDone(something<T>&& res){
			d = true;
			r = res;
		}
		void setDone(const T& res){ setDone(something<T>(res)); }
		void setUndone(){
			d = false;
		}
};

template<typename T> Future<T> completed(T t){
	std::shared_ptr<OutsideFuture<T>> vf(new OutsideFuture<T>());
	vf->setDone(t);
	return vf;
} 

template<typename T> Future<T> asyncSleep(Yengine* engine, unsigned ms, T ret){
	std::shared_ptr<OutsideFuture<T>> f(new OutsideFuture<T>());
	std::thread th([engine, ms](std::shared_ptr<OutsideFuture<T>> f, something<T> rt){
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
		f->setDone(rt);
		engine->notify(std::dynamic_pointer_cast<IFutureT<T>>(f));
	}, f, something<T>(ret));
	th.detach();
	return f;
}

}
