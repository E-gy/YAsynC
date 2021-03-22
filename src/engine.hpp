#pragma once

#include <unordered_map>

#include <optional>
#include <memory>
#include <any>
#include <tuple>
#include <thread>

#include "future.hpp"
#include "threadsafequeue.hpp"

#ifdef _DEBUG
#include <iostream>
#endif

namespace yasync {

/**
 * Transforms a generator into a future
 * @param gen the generator
 * @returns future
 */ 
template<typename T> Genf<T> defer(Generator<T> gen){
	return std::make_shared<IGenfT<T>>(gen);
}

/*
 * Generators indicators
 * `T` a *t*hing
 * `T^` future thing(s)
 * `T?` maybe a thing
 * `T...` a few things (superscripts can be used to indicate count conservation)
 * ⇒ `T?..` maybe a few things
 * `→` direct
 * `↠` explosion
 * `↣` implosion
 * PS: Futures _can_ always yield more than one thing by design (obviously it requires cooperation on both sides). Therefor ~~explosion~~ many-yield of `T^` into `T...` is implicit and will not be denoted by the arrows.
 */

/**
 * U → V
 * F: U → V
 */
template<typename V, typename U, typename F> class OpDirectGenerator0 : public IGeneratorT<V> {
	enum class State {
		I, A, Fi
	};
	State state = State::I;
	Future<U> awa;
	F f;
	public:
		OpDirectGenerator0(Future<U> w, F && map) : awa(w), f(std::move(map)) {}
		bool done() const override { return state == State::Fi; }
		Generesume<V> resume(const Yengine*) override {
			switch(state){
				case State::I:
					state = State::A;
					return awa;
				case State::A:
					state = awa.state() == FutureState::Completed ? State::Fi : State::I;
					if constexpr (std::is_same<V, void>::value){
						if constexpr (std::is_same<U, void>::value) f();
						else f(awa.result());
						return monoid<void>();
					} else if constexpr (std::is_same<U, void>::value) return monoid<V>(f());
					else return monoid<V>(f(awa.result()));
				case State::Fi: //Never
				default:
					return awa;
			}
		}
};

/**
 * U? → V?
 * F: U → V|V?
 */
template<typename V, typename U, typename F> class OpDirectGeneratorMM : public IGeneratorT<Maybe<V>> {
	enum class State {
		I, A, Fi
	};
	State state = State::I;
	Future<Maybe<U>> awa;
	F f;
	public:
		OpDirectGeneratorMM(Future<Maybe<U>> w, F && map) : awa(w), f(std::move(map)) {}
		bool done() const override { return state == State::Fi; }
		Generesume<Maybe<V>> resume(const Yengine*) override {
			switch(state){
				case State::I:
					state = State::A;
					return awa;
				case State::A: {
					if(awa.state() == FutureState::Completed) state = State::Fi;
					auto awar = awa.result();
					if(awar){ //Precursor has a thing
						Maybe<V> r;
						if constexpr (std::is_same<U, void>::value){
							using FV = std::decay_t<decltype(f())>;
							if constexpr (std::is_same<Maybe<V>, FV>::value) r = f();
							else if constexpr (std::is_same<V, void>::value){ f(); r = Maybe<V>(true); }
							else r = Maybe<V>{f()};
						}
						else {
							using FV = std::decay_t<decltype(f(*awar))>;
							if constexpr (std::is_same<Maybe<V>, FV>::value) r = f(std::move(*awar));
							else if constexpr (std::is_same<V, void>::value){ f(std::move(*awar)); r = Maybe<V>(true); }
							else r = Maybe<V>{f(std::move(*awar))};
						}
						if(state == State::Fi) return r; //Precursor is done, return whatever we made
						else if(r){ //Precursor is not done, but we made use of last value
							state = State::I;
							return r;
						} else return awa; //Precursor is done, and we didn't use last value
					} else {
						if(state == State::Fi) return Maybe<V>{}; //Precursor is done, and there was no more
						else return awa; //Precursor is not done, just returned nothing for some reason
					}
				}
				case State::Fi:
				default:
					return Maybe<V>{};
			}
		}
};

/**
 * U → V...
 * F: U → V^
 */
template<typename V, typename U, typename F> class OpDirectGeneratorF : public IGeneratorT<V> {
	enum class State {
		I, A0, A1r, A1, Fi
	};
	State state = State::I;
	Future<U> awa;
	std::optional<Future<V>> nxt = std::nullopt;
	F f;
	public:
		OpDirectGeneratorF(Future<U> w, F && gf) : awa(w), f(std::move(gf)) {}
		bool done() const override { return state == State::Fi; }
		Generesume<V> resume(const Yengine*) override {
			switch(state){
				case State::I:
					state = State::A0;
					return awa;
				case State::A0: {
					if constexpr (std::is_same<U, void>::value) nxt = f();
					else nxt = f(awa.result());
					[[fallthrough]];
				}
				case State::A1r:
					state = State::A1;
					return *nxt;
				case State::A1: {
					if(nxt->state() == FutureState::Completed)
						if(awa.state() == FutureState::Completed) state = State::Fi;
						else {
							state = State::I;
							nxt = std::nullopt;
						}
					else state = State::A1r;
					if constexpr (std::is_same<V, void>::value) return monoid<V>();
					else return nxt->result();
				}
				case State::Fi: //Never
				default:
					return awa;
			}
		}
};

/**
 * U? → V?..
 * F: U → V^|V^?
 */
template<typename V, typename U, typename F> class OpDirectGeneratorFMM : public IGeneratorT<Maybe<V>> {
	enum class State {
		I, A0, A1r, A1, Fi
	};
	State state = State::I;
	Future<Maybe<U>> awa;
	std::optional<Future<V>> nxt = std::nullopt;
	F f;
	public:
		OpDirectGeneratorFMM(Future<Maybe<U>> w, F && gf) : awa(w), f(std::move(gf)) {}
		bool done() const override { return state == State::Fi; }
		Generesume<V> resume(const Yengine*) override {
			switch(state){
				case State::I:
					state = State::A0;
					return awa;
				case State::A0: {
					if(awa.state() == FutureState::Completed) state = State::Fi;
					auto awar = awa.result();
					if(awar){ //Precursor has a thing
						Maybe<Future<V>> nx;
						if constexpr (std::is_same<U, void>::value) nx = f();
						else nx = f(std::move(*awar));
						if(nx){ //We made use of thing
							state = State::A1;
							return *(nxt = *nx);
						} else if(state == State::Fi) return Maybe<V>{}; //We didn't make use of thing, and precursor is done
						else return awa; //We didn't make use of thing, but precursor is not done
					} else {
						if(state == State::Fi) return Maybe<V>{}; //Precursor is done, and there was no more
						else return awa; //Precursor is not done, just returned nothing for some reason
					}
				}
				case State::A1r:
					state = State::A1;
					return *nxt;
				case State::A1: {
					if(nxt->state() == FutureState::Completed) //our thingy completed
						if(awa.state() == FutureState::Completed) state = State::Fi; //and precursor completed
						else {
							state = State::I;
							nxt = std::nullopt;
						}
					else state = State::A1r; //our thingy has more thingies
					if constexpr (std::is_same<V, void>::value) return Maybe<V>(true);
					else return Maybe<V>{nxt->result()};
				}
				case State::Fi:
				default:
					return Maybe<V>{};
			}
		}
};

template <typename T> struct _typed{};
template <typename V, typename U, typename F> Future<V> then_spec1(Future<U> f, F && map, _typed<V>){
	return defer(Generator<V>(new OpDirectGenerator0<V, U, F>(f, std::move(map))));
}
template <typename V, typename U, typename F> Future<V> then_spec1(Future<U> f, F && map, _typed<Future<V>>){
	return defer(Generator<V>(new OpDirectGeneratorF<V, U, F>(f, std::move(map))));
}
template <typename V, typename U, typename F> Future<Maybe<V>> then_spec1(Future<Maybe<U>> f, F && map, _typed<V>){
	return defer(Generator<Maybe<V>>(new OpDirectGeneratorMM<V, U, F>(f, std::move(map))));
}
template <typename V, typename U, typename F> Future<Maybe<V>> then_spec1(Future<Maybe<U>> f, F && map, _typed<Future<V>>){
	return defer(Generator<Maybe<V>>(new OpDirectGeneratorFMM<V, U, F>(f, std::move(map))));
}
template <typename V, typename U, typename F> Future<Maybe<V>> then_spec1(Future<Maybe<U>> f, F && map, _typed<Maybe<V>>){
	return defer(Generator<Maybe<V>>(new OpDirectGeneratorMM<V, U, F>(f, std::move(map))));
}
template <typename V, typename U, typename F> Future<Maybe<V>> then_spec1(Future<Maybe<U>> f, F && map, _typed<Future<Maybe<V>>>){
	return defer(Generator<Maybe<V>>(new OpDirectGeneratorFMM<V, U, F>(f, std::move(map))));
}

template<typename U, typename F> auto then_spec2(Future<U> f, F && map){
	if constexpr (std::is_same<U, void>::value){
		using V = std::decay_t<decltype(map())>;
		return then_spec1(f, std::move(map), _typed<V>{});
	} else {
		using V = std::decay_t<decltype(map(f.result()))>;
		return then_spec1(f, std::move(map), _typed<V>{});
	}
}
template<typename U, typename F> auto then_spec2(Future<Maybe<U>> f, F && map){
	if constexpr (std::is_same<U, void>::value){
		using V = std::decay_t<decltype(map())>;
		return then_spec1(f, std::move(map), _typed<V>{});
	} else {
		using V = std::decay_t<decltype(map(*f.result()))>;
		return then_spec1(f, std::move(map), _typed<V>{});
	}
}

/**
 * Transforms future value(s) by (a)synchronous function
 * @param f @ref future to map
 * @param map `(ref in: U) -> V|Future<V>` function taking a reference to input type thing and producing output type thing or future
 * @returns @ref
 */
template<typename U, typename F> auto then(Future<U> f, F && map){
	return then_spec2(f, map);
}

template<typename U, typename F> auto operator>>(Future<U> f, F && map){
	return then(f, std::move(map));
}
template<typename U, typename F> auto operator>>(std::shared_ptr<U> f, F && map){
	return then(Future(f, *f), std::move(map));
}

/**
 * U ↠ V?..
 * F: U ↠ V?..
 */
template<typename V, typename Vs, typename U, typename F> class OpExplodingGenerator : public IGeneratorT<Maybe<V>> {
	enum class State {
		I, A0, Ar, Fi
	};
	State state = State::I;
	Future<U> awa;
	Vs im;
	typename Vs::iterator imp;
	F f;
	public:
		OpExplodingGenerator(Future<U> w, F && exp) : awa(w), f(std::move(exp)) {}
		bool done() const override { return state == State::Fi; }
		Generesume<Maybe<V>> resume(const Yengine*) override {
			switch(state){
				case State::I:
					state = State::A0;
					return awa;
				case State::A0:
					if constexpr (std::is_same<U, void>::value) im = f();
					else im = f(awa.result());
					if((imp = im.begin()) == im.end()){ //Explosion resulted in nothing
						if(awa.state() == FutureState::Completed){ //and precursor is done
							state = State::Fi;
							return Maybe<V>{};
						} else return awa; //Precursor is not done
					}
					state = State::Ar;
					[[fallthrough]];
				case State::Ar: {
					auto v = std::move(*imp);
					if(++imp == im.end()) state = awa.state() == FutureState::Completed ? State::Fi : State::I;
					return Maybe<V>(std::move(v));
				}
				case State::Fi:
				default:
					return Maybe<V>{};
			}
		}
};

/**
 * U? ↠ V?..
 * F: U ↠ V?..
 */
template<typename V, typename Vs, typename U, typename F> class OpExplodingGeneratorM : public IGeneratorT<Maybe<V>> {
	enum class State {
		I, A0, Ar, Fi
	};
	State state = State::I;
	Future<Maybe<U>> awa;
	Vs im;
	typename Vs::iterator imp;
	F f;
	public:
		OpExplodingGeneratorM(Future<Maybe<U>> w, F && exp) : awa(w), f(std::move(exp)) {}
		bool done() const override { return state == State::Fi; }
		Generesume<Maybe<V>> resume(const Yengine*) override {
			switch(state){
				case State::I:
					state = State::A0;
					return awa;
				case State::A0: {
					auto awar = awa.result();
					if(!awar){ //Precursor got nothing
						if(awa.state() == FutureState::Completed){ //and precursor is done
							state = State::Fi;
							return Maybe<V>{};
						} else return awa; //Precursor is not done, just returned nothing for some reason
					}
					if constexpr (std::is_same<U, void>::value) im = f();
					else im = f(std::move(*awar));
					if((imp = im.begin()) == im.end()){ //Explosion resulted in nothing
						if(awa.state() == FutureState::Completed){ //and precursor is done
							state = State::Fi;
							return Maybe<V>{};
						} else return awa; //Precursor is not done
					}
					state = State::Ar;
					[[fallthrough]];
				}
				case State::Ar: {
					auto v = std::move(*imp);
					if(++imp == im.end()) state = awa.state() == FutureState::Completed ? State::Fi : State::I;
					return Maybe<V>(std::move(v));
				}
				case State::Fi:
				default:
					return Maybe<V>{};
			}
		}
};

template<typename V, typename Vs, typename U, typename F> Future<Maybe<V>> explode_spec0(Future<U> f, F && exp, _typed<Vs>, _typed<V>){
	return defer(Generator<Maybe<V>>(new OpExplodingGenerator<V, Vs, U, F>(f, std::move(exp))));
}
template<typename V, typename Vs, typename U, typename F> Future<Maybe<V>> explode_spec0(Future<Maybe<U>> f, F && exp, _typed<Vs>, _typed<V>){
	return defer(Generator<Maybe<V>>(new OpExplodingGeneratorM<V, Vs, U, F>(f, std::move(exp))));
}
template<typename U, typename F> auto explode_spec1(Future<U> f, F && exp){
	using Vs = std::decay_t<decltype(exp(f.result()))>;
	return explode_spec0(f, std::move(exp), _typed<Vs>{}, _typed<typename Vs::value_type>{});
}
template<typename U, typename F> auto explode_spec1(Future<Maybe<U>> f, F && exp){
	using Vs = std::decay_t<decltype(exp(*f.result()))>;
	return explode_spec0(f, std::move(exp), _typed<Vs>{}, _typed<typename Vs::value_type>{});
}

template<typename U, typename F> auto explode(Future<U> f, F && exp){
	return explode_spec1(f, std::move(exp));
}
template<typename U, typename F> auto operator<<(Future<U> f, F && exp){
	return explode(f, std::move(exp));
}
template<typename U, typename F> auto operator<<(std::shared_ptr<U> f, F && exp){
	return explode(Future(f, *f), std::move(exp));
}

/**
 * U? ↣ V
 * F: () → V
 *  | (V, U) → V
 */
template<typename V, typename U, typename F> class OpImplodeGeneratorM : public IGeneratorT<V> {
	enum class State {
		I, A, Fi
	};
	State state = State::I;
	Future<Maybe<U>> awa;
	F f;
	monoid<V> acc;
	public:
		OpImplodeGeneratorM(Future<Maybe<U>> w, F && map) : awa(w), f(std::move(map)) {
			if constexpr (std::is_same<V, void>::value) acc = monoid<V>();
			else acc = f();
		}
		bool done() const override { return state == State::Fi; }
		Generesume<V> resume(const Yengine*) override {
			switch(state){
				case State::I:
					state = State::A;
					return awa;
				case State::A: {
					if(awa.state() == FutureState::Completed) state = State::Fi;
					auto awar = awa.result();
					if(awar){ //Precursor has a thing
						if constexpr (std::is_same<V, void>::value) f(std::move(*awar));
						else acc = f(acc.move(), std::move(*awar));
					}
					if(state == State::Fi) return std::move(acc); //Precursor is done
					else return awa; //Precursor has more
				}
				case State::Fi: //Never
				default:
					return awa;
			}
		}
};

template<typename T> struct identity { using type = T; };

template<typename T> using noargscall_t = decltype(std::declval<T>()());
template<typename, typename = std::void_t<>> struct has_noargscall : std::false_type {};
template<typename T> struct has_noargscall<T, std::void_t<noargscall_t<T>>> : std::true_type {};

template<typename V, typename U, typename F> Future<V> implode_spec0(Future<Maybe<U>> f, F && imp, _typed<V>){
	return defer(Generator<V>(new OpImplodeGeneratorM<V, U, F>(f, std::move(imp))));
}
template<typename U, typename F> auto implode_spec1(Future<U> f, F && imp){
	if constexpr (has_noargscall<F>::value) {
		using V = noargscall_t<F>;
		return implode_spec0(f, std::move(imp), _typed<V>{});
	}
	else return implode_spec0(f, std::move(imp), _typed<void>{});
}

template<typename U, typename F> auto implode(Future<U> f, F && exp){
	return implode_spec1(f, std::move(exp));
}
template<typename U, typename F> auto operator>>=(Future<U> f, F && exp){
	return implode(f, std::move(exp));
}
template<typename U, typename F> auto operator>>=(std::shared_ptr<U> f, F && exp){
	return implode(Future(f, *f), std::move(exp));
}

template<typename V, typename F, typename... State> class GeneratorLGenerator : public IGeneratorT<V> {
	bool d = false;
	std::tuple<State...> state;
	F g;
	public:
		GeneratorLGenerator(std::tuple<State...> s, F && gen) : state(s), g(std::move(gen)){}
		bool done() const override { return d; }
		Generesume<V> resume(const Yengine* engine) override {
			return g(engine, d, state);
		}
};

template<typename V, typename F, typename... State> Generator<V> lambdagen_spec(_typed<std::variant<AFuture, monoid<V>>>, F && f, State... args){
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
		Generesume<V> resume(const Yengine* engine) override {
			return g(engine, d, state);
		}
};

template<typename V, typename F, typename S> Generator<V> lambdagen_spec(_typed<std::variant<AFuture, monoid<V>>>, F && f, S arg){
	return Generator<V>(new GeneratorLGenerator<V, F, S>(arg, std::move(f)));
}

template<typename F, typename S> auto lambdagen(F && f, S arg){
	const Yengine* engine;
	bool don;
	S _arg;
	using V = std::decay_t<decltype(f(engine, don, _arg))>;
	return lambdagen_spec(_typed<V>{}, std::move(f), arg);
}

class Yengine {
	/**
	 * Queue<Future<?>>
	 */
	ThreadSafeQueue<AFuture> work;
	/**
	 * Future → Future
	 */
	std::unordered_map<AFuture, AGenf> notifications;
	unsigned workers;
	std::vector<std::thread> workets;
	public:
		Yengine(unsigned threads);
		void wle();
		void execute(const AGenf&);
		void notify(const ANotf&);
		/**
		 * Resumes parallel yield of the future
		 * @param f future to execute
		 * @returns f
		 */ 
		template<typename T> inline decltype(auto) execute(const Genf<T>& f){
			execute(std::static_pointer_cast<IGenf>(f));
			return f;
		}
		/**
		 * Transforms the generator into a future on this engine, and executes in parallel
		 * @param gen the generator
		 * @returns future
		 */ 
		template<typename T> inline Genf<T> launch(Generator<T> gen){
			return execute(defer(gen));
		}
		/**
		 * Notifies the engine of completion, or cancellation, of an external future.
		 * Returns almost immediately - actual processing of the notification will happen internally.
		 * !!!USE CANCELLATION WITH CARE!!!
		 * On cancellation the entire awaited chain is yeeted into oblivion.
		 */
		template<typename T> inline decltype(auto) notify(const Notf<T>& f){
			notify(std::static_pointer_cast<INotf>(f));
			return f;
		}
		/**
		 * Notifies the engine of cancellation of an external future.
		 * !!!USE CANCELLATION WITH CARE!!!
		 * On cancellation the entire awaited chain is yeeted into oblivion.
		 * 
		 * _It is just an alias for notify._
		 */
		template<typename T> inline void cancelled(const Notf<T>& f){ notify(f); }
		/**
		 * Submits generative future for execution.
		 * No-op if the future is not generative.
		 * @see Yengine::execute
		 */
		template<typename T> inline decltype(auto) operator<<=(const Future<T>& f){
			if(auto gf = f.genf()) execute(*gf);
			return f;
		}
	private:
		std::condition_variable condWLE;
		std::mutex notificationsLock;
		void notifiAdd(AFuture k, AGenf v);
		std::optional<AGenf> notifiDrop(AFuture k);
		void threado(AFuture task);
		void threadwork();
};

/**
 * Submits generative future for execution.
 * @see Yengine::execute
 * @returns f
 */
template<typename T> inline decltype(auto) operator<<=(Yengine* engine, const Genf<T>& gf){ return engine->execute(gf); }

/**
 * Submits generative future for execution.
 * No-op if the future is not generative.
 * @see Yengine::execute
 */
template<typename T> inline decltype(auto) operator<<=(Yengine* engine, const Future<T>& f){ return *engine <<= f; }

}
