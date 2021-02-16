#pragma once

#include <unordered_map>

#include <optional>
#include <memory>
#include <any>
#include <tuple>

#include "agen.hpp"
#include "future.hpp"
#include "threadsafequeue.hpp"

namespace yasync {

template<typename T> class FutureG : public IFutureT<T> {
	public:
		Generator<T> gen;
		FutureState s = FutureState::Suspended;
		std::optional<something<T>> val;
		FutureG(Generator<T> g) : gen(g) {}
		FutureState state(){ return s; }
		std::optional<something<T>> result(){ return val; }
};

/**
 * Transforms a generator into a future
 * @param gen the generator
 * @returns future
 */ 
template<typename T> Future<T> defer(Generator<T> gen){
	return Future<T>(new FutureG<T>(gen));
}

template<typename T> class IdentityGenerator : public IGeneratorT<T> {
	Future<T> w;
	bool reqd = false;
	public:
		IdentityGenerator(Future<T> awa) : w(awa) {}
		bool done() const { return reqd && w->state() == FutureState::Completed; }
		std::variant<AFuture, something<T>> resume([[maybe_unused]] const Yengine* engine){
			if(w->state() == FutureState::Completed || !(reqd = !reqd)){
				if constexpr (std::is_same<T, void>::value) return something<void>();
				else return something<T>(*(w->result()));
			} else return w;
		}
};

template<typename V, typename U, typename F> class ChainingGenerator : public IGeneratorT<V> {
	Future<U> w;
	bool reqd = false;
	F f;
	public:
		ChainingGenerator(Future<U> awa, F map) : w(awa), f(map) {}
		bool done() const { return reqd && w->state() == FutureState::Completed; }
		std::variant<AFuture, something<V>> resume([[maybe_unused]] const Yengine* engine){
			if(w->state() == FutureState::Completed || !(reqd = !reqd)){
				if constexpr (std::is_same<V, void>::value){
					f(*(w->result()));
					return something<void>();
				} else return something<V>(f(*(w->result())));
			} else return w;
		}
};

template<typename V, typename U, typename F> class ChainingWrappingGenerator : public IGeneratorT<V> {
	enum class State {
		I, A0, A1r, A1, Fi
	};
	State state = State::I;
	Future<U> awa;
	std::optional<Future<V>> nxt = std::nullopt;
	F gf;
	public:
		ChainingWrappingGenerator(Future<U> w, F f) : awa(w), gf(f) {}
		bool done() const { return state == State::Fi; }
		std::variant<AFuture, something<V>> resume([[maybe_unused]] const Yengine* engine){
			switch(state){
				case State::I:
					state = State::A0;
					return awa;
				case State::A0: {
					Future<V> f1 = gf(*(awa->result()));
					nxt = f1;
					[[fallthrough]];
				}
				case State::A1r:
					state = State::A1;
					return *nxt;
				case State::A1: {
					auto f1 = *nxt;
					if(f1->state() == FutureState::Completed)
						if(awa->state() == FutureState::Completed) state = State::Fi;
						else {
							state = State::I;
							nxt = std::nullopt;
						}
					else state = State::A1r;
					return *(f1->result());
				}
				case State::Fi:
					return *((*nxt)->result());
				default: //never
					return awa;
			}
		}
};

template <typename T> struct _typed{};
template <typename V, typename U, typename F> Future<V> then_spec(Future<U> f, F map, _typed<V>){
	return defer(Generator<V>(new ChainingGenerator<V, U, F>(f, map)));
}
template <typename V, typename U, typename F> Future<V> then_spec(Future<U> f, F map, _typed<Future<V>>){
	return defer(Generator<V>(new ChainingWrappingGenerator<V, U, F>(f, map)));
}

/**
 * Transforms future value(s) by (a)synchronous function
 * @param f @ref future to map
 * @param map `(ref in: U) -> V|Future<V>` function taking a reference to input type thing and producing output type thing or future
 * @returns @ref
 */
template<typename U, typename F> auto then(Future<U> f, F map){
	using V = std::decay_t<decltype(map(*(f->result())))>;
	return then_spec(f, map, _typed<V>{});
}

template<typename U, typename F> auto operator>>(Future<U> f, F map){
	return then(f, map);
}

template<typename V, typename F, typename... State> class GeneratorLGenerator : public IGeneratorT<V> {
	bool d = false;
	std::tuple<State...> state;
	F g;
	public:
		GeneratorLGenerator(std::tuple<State...> s, F gen) : state(s), g(gen){}
		bool done() const { return d; }
		std::variant<AFuture, something<V>> resume(const Yengine* engine){
			return g(engine, d, state);
		}
};

template<typename V, typename F, typename... State> Generator<V> lambdagen_spec(_typed<std::variant<AFuture, something<V>>>, F f, State... args){
	return Generator<V>(new GeneratorLGenerator<V, F, State...>(std::tuple<State...>(args...), f));
}

template<typename F, typename... State> auto lambdagen(F f, State... args){
	const Yengine* engine;
	bool don;
	using V = std::decay_t<decltype(f(engine, don, std::tuple<State...>()))>;
	return lambdagen_spec(_typed<V>{}, f, args...);
}

template<typename V, typename F, typename S> class GeneratorLGenerator<V, F, S> : public IGeneratorT<V> {
	bool d = false;
	S state;
	F g;
	public:
		GeneratorLGenerator(S s, F gen) : state(s), g(gen){}
		bool done() const { return d; }
		std::variant<AFuture, something<V>> resume(const Yengine* engine){
			return g(engine, d, state);
		}
};

template<typename V, typename F, typename S> Generator<V> lambdagen_spec(_typed<std::variant<AFuture, something<V>>>, F f, S arg){
	return Generator<V>(new GeneratorLGenerator<V, F, S>(arg, f));
}

template<typename F, typename S> auto lambdagen(F f, S arg){
	const Yengine* engine;
	bool don;
	using V = std::decay_t<decltype(f(engine, don, arg))>;
	return lambdagen_spec(_typed<V>{}, f, arg);
}

/**
 * @see then but with manual type parametrization
 */
template<typename V, typename U, typename F> Future<V> them(Future<U> f, F map){
	using t_rt = std::decay_t<decltype(map(*(f->result())))>;
	if constexpr (std::is_convertible<t_rt, AFuture>::value) return defer(Generator<V>(new ChainingWrappingGenerator<V, U, F>(f, map)));
	else return defer(Generator<V>(new ChainingGenerator<V, U, F>(f, map)));
}

class Yengine {
	/**
	 * Queue<FutureG<?>>
	 */
	ThreadSafeQueue<AFuture> work;
	/**
	 * Future → Future
	 */
	std::unordered_map<AFuture, AFuture> notifications;
	unsigned workers;
	public:
		Yengine(unsigned threads);
		void wle();
		/**
		 * Resumes parallel yield of the future
		 * @param f future to execute
		 * @returns f
		 */ 
		template<typename T> Future<T> execute(Future<T> f){
			auto ft = std::dynamic_pointer_cast<FutureG<T>>(f);
			ft->s = FutureState::Queued;
			work.push(f);
			return f;
		}
		template<typename T> auto operator<<=(Future<T> f){
			return execute(f);
		}
		/**
		 * Transforms the generator into a future on this engine, and executes in parallel
		 * @param gen the generator
		 * @returns future
		 */ 
		template<typename T> Future<T> launch(Generator<T> gen){
			return execute(defer(gen));
		}
		/**
		 * Notifies the engine of completion of an external future.
		 * Returns almost immediately - actual processing of the notification will happen internally.
		 */
		template<typename T> void notify(Future<T> f){
			if(auto noti = notifiDrop(f)){
				auto redir = defer(Generator<T>(new IdentityGenerator<T>(f)));
				notifiAdd(redir, *noti);
				execute(redir);
			}
		}
	private:
		std::condition_variable condWLE;
		std::mutex notificationsLock;
		void notifiAdd(AFuture k, AFuture v){
			std::unique_lock lock(notificationsLock);
			notifications[k] = v;
		}
		std::optional<AFuture> notifiDrop(AFuture k){
			std::unique_lock lock(notificationsLock);
			auto naut = notifications.find(k);
			if(naut == notifications.end()) return std::nullopt;
			auto ret = naut->second;
			notifications.erase(naut);
			if(notifications.empty()) condWLE.notify_all();
			return ret;
		}
		void threado(AFuture task){
			if(task->state() > FutureState::Running) return; //Only suspended tasks are resumeable
			//cont:
			while(true)
			{
			auto gent = (FutureG<void*>*) task.get();
			gent->s = FutureState::Running;
			auto g = gent->gen->resume(this);
			if(auto awa = std::get_if<AFuture>(&g)){
				switch((*awa)->state()){
					case FutureState::Completed:
						// goto cont;
						break;
					case FutureState::Suspended:
						gent->s = FutureState::Awaiting;
						notifiAdd(*awa, task);
						task = *awa;
						// goto cont;
						break;
					case FutureState::Queued: //This is stoopid, but hey we don't want to sync what we don't need, so it'll wait
					case FutureState::Awaiting:
					case FutureState::Running:
						gent->s = FutureState::Awaiting;
						notifiAdd(*awa, task);
						return;
				}
			} else {
				auto v = std::get_if<something<void*>>(&g); //do NOT!!! copy. C++ compiler reaaally wants to copy. NO!
				gent->s = gent->gen->done() ? FutureState::Completed : FutureState::Suspended;
				gent->val.emplace(*v);
				//#BeLazy: Whether we're done or not, drop from notifications. If we're done, well that's it. If we aren't, someone up in the pipeline will await for us at some point, setting up the notifications once again.
				if(auto naut = notifiDrop(task)) task = *naut; //Proceed up the await chain immediately
				else return;
			}
			}
		}
		void threadwork(){
			while(auto w = work.pop()){ //TODO !closed
				threado(*w);
			}
		}
};

}
