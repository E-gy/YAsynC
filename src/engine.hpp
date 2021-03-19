#pragma once

#include <unordered_map>

#include <optional>
#include <memory>
#include <any>
#include <tuple>

#include "agen.hpp"
#include "future.hpp"
#include "threadsafequeue.hpp"

#ifdef _DEBUG
#include <iostream>
#endif

namespace yasync {

class IFutureGa {};

template<typename T> class IFutureG : public IFutureT<T>, public IFutureGa {
	protected:
		FutureState s = FutureState::Suspended;
		movonly<T> val;
	public:
		const Generator<T> gen;
		IFutureG(Generator<T> g) : gen(g) {}
		FutureState state() const override { return s; }
		movonly<T> result() override { return std::move(val); }
		void onQueued() override { s = FutureState::Queued; }
		void set(FutureState state){ s = state; }
		void set(FutureState state, movonly<T> && v){
			set(state);
			val = std::move(v);
		}
		#ifdef _DEBUG
		bool isExternal() override { return false; }
		#endif
};

using FutureGa = std::shared_ptr<IFutureGa>;
template<typename T> using FutureG = std::shared_ptr<IFutureG<T>>;

/**
 * Transforms a generator into a future
 * @param gen the generator
 * @returns future
 */ 
template<typename T> Future<T> defer(Generator<T> gen){
	return std::make_shared<IFutureG<T>>(gen);
}

template<typename V, typename U, typename F> class ChainingGenerator : public IGeneratorT<V> {
	Future<U> w;
	bool reqd = false;
	F f;
	public:
		ChainingGenerator(Future<U> awa, F && map) : w(awa), f(std::move(map)) {}
		bool done() const override { return reqd && w->state() == FutureState::Completed; }
		std::variant<AFuture, movonly<V>> resume(const Yengine*) override {
			if(w->state() == FutureState::Completed || !(reqd = !reqd)){
				if constexpr (std::is_same<V, void>::value){
					if constexpr (std::is_same<U, void>::value) f();
					else f(*w->result());
					return movonly<void>();
				} else if constexpr (std::is_same<U, void>::value) return movonly<V>(f());
				else return movonly<V>(f(*w->result()));
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
		ChainingWrappingGenerator(Future<U> w, F && f) : awa(w), gf(std::move(f)) {}
		bool done() const override { return state == State::Fi; }
		std::variant<AFuture, movonly<V>> resume(const Yengine*) override {
			switch(state){
				case State::I:
					state = State::A0;
					return awa;
				case State::A0: {
					Future<V> f1;
					if constexpr (std::is_same<U, void>::value) f1 = gf();
					else f1 = gf(*awa->result());
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
					return f1->result();
				}
				case State::Fi:
					return (*nxt)->result();
				default: //never
					return awa;
			}
		}
};

template <typename T> struct _typed{};
template <typename V, typename U, typename F> Future<V> then_spec(Future<U> f, F && map, _typed<V>){
	return defer(Generator<V>(new ChainingGenerator<V, U, F>(f, std::move(map))));
}
template <typename V, typename U, typename F> Future<V> then_spec(Future<U> f, F && map, _typed<Future<V>>){
	return defer(Generator<V>(new ChainingWrappingGenerator<V, U, F>(f, std::move(map))));
}

/**
 * Transforms future value(s) by (a)synchronous function
 * @param f @ref future to map
 * @param map `(ref in: U) -> V|Future<V>` function taking a reference to input type thing and producing output type thing or future
 * @returns @ref
 */
template<typename U, typename F> auto then(Future<U> f, F && map){
	if constexpr (std::is_same<U, void>::value){
		using V = std::decay_t<decltype(map())>;
		return then_spec(f, std::move(map), _typed<V>{});
	} else {
		using V = std::decay_t<decltype(map(*f->result()))>;
		return then_spec(f, std::move(map), _typed<V>{});
	}
}

template<typename U, typename F> auto operator>>(Future<U> f, F && map){
	return then(f, std::move(map));
}

template<typename V, typename F, typename... State> class GeneratorLGenerator : public IGeneratorT<V> {
	bool d = false;
	std::tuple<State...> state;
	F g;
	public:
		GeneratorLGenerator(std::tuple<State...> s, F && gen) : state(s), g(std::move(gen)){}
		bool done() const override { return d; }
		std::variant<AFuture, movonly<V>> resume(const Yengine* engine) override {
			return g(engine, d, state);
		}
};

template<typename V, typename F, typename... State> Generator<V> lambdagen_spec(_typed<std::variant<AFuture, movonly<V>>>, F && f, State... args){
	return Generator<V>(new GeneratorLGenerator<V, F, State...>(std::tuple<State...>(args...), std::move(f)));
}

template<typename F, typename... State> auto lambdagen(F && f, State... args){
	const Yengine* engine;
	bool don;
	using V = std::decay_t<decltype(f(engine, don, std::tuple<State...>()))>;
	return lambdagen_spec(_typed<V>{}, std::move(f), args...);
}

template<typename V, typename F, typename S> class GeneratorLGenerator<V, F, S> : public IGeneratorT<V> {
	bool d = false;
	S state;
	F g;
	public:
		GeneratorLGenerator(S s, F && gen) : state(s), g(std::move(gen)){}
		bool done() const override { return d; }
		std::variant<AFuture, movonly<V>> resume(const Yengine* engine) override {
			return g(engine, d, state);
		}
};

template<typename V, typename F, typename S> Generator<V> lambdagen_spec(_typed<std::variant<AFuture, movonly<V>>>, F && f, S arg){
	return Generator<V>(new GeneratorLGenerator<V, F, S>(arg, std::move(f)));
}

template<typename F, typename S> auto lambdagen(F && f, S arg){
	const Yengine* engine;
	bool don;
	S _arg;
	using V = std::decay_t<decltype(f(engine, don, _arg))>;
	return lambdagen_spec(_typed<V>{}, std::move(f), arg);
}

/**
 * @see then but with manual type parametrization
 */
template<typename V, typename U, typename F> Future<V> them(Future<U> f, F && map){
	using t_rt = std::decay_t<decltype(map(*(f->result())))>;
	if constexpr (std::is_convertible<t_rt, AFuture>::value) return defer(Generator<V>(new ChainingWrappingGenerator<V, U, F>(f, std::move(map))));
	else return defer(Generator<V>(new ChainingGenerator<V, U, F>(f, std::move(map))));
}

class Yengine {
	/**
	 * Queue<Future<?>>
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
			f->onQueued();
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
		 * Notifies the engine of completion, or cancellation, of an external future.
		 * Returns almost immediately - actual processing of the notification will happen internally.
		 * !!!USE CANCELLATION WITH CARE!!!
		 * On cancellation the entire awaited chain is yeeted into oblivion.
		 */
		void notify(AFuture f);
		/**
		 * Notifies the engine of cancellation of an external future.
		 * !!!USE CANCELLATION WITH CARE!!!
		 * On cancellation the entire awaited chain is yeeted into oblivion.
		 * 
		 * _It is just an alias for notify._
		 */
		template<typename T> void cancelled(Future<T> f){ notify(f); }
	private:
		std::condition_variable condWLE;
		std::mutex notificationsLock;
		void notifiAdd(AFuture k, AFuture v);
		std::optional<AFuture> notifiDrop(AFuture k);
		void threado(AFuture task);
		void threadwork();
};

template<typename T> auto operator<<=(Yengine* const engine, Future<T> f){ return engine->execute(f); }

}
