#pragma once

#include "afuture.hpp"
#include "captrace.hpp"
#include <optional>

namespace yasync {

class Yengine;

template<typename T> using Generesume = std::variant<AFuture, monoid<T>>;

/**
 * Generic asynchronous generator interface.
 * - `done` must always indicate whether the generation has completed (even prior to initial `resume` call)
 * - the generator can transition only from `!done` into `done` state and only on a producing [ret none] `resume`.
 * - `resume` will not be called again after the generator goes into `done` state
 */
template<typename T> class IGeneratorT {
	public:
		/**
		 * @returns whether the generation has finished
		 */
		virtual bool done() const = 0;
		/**
		 * Resumes the generation process.
		 * Invoked as initialization, prior to any other invocations.
		 * Returning a future means that the generator is waiting for that future's completion, and this function will be invoked again once it completes.
		 * Returning a value is well - producing the value, and this function will be invoked again iff the generator is not done when the next value is requested.
		 * 
		 * @param engine @ref async engine to launch tasks in parallel
		 * @returns @produces the next value if ready, the future this generator is awaiting for otherwise
		 */
		virtual Generesume<T> resume(const Yengine* engine) = 0;
};

template<typename T> using Generator = std::shared_ptr<IGeneratorT<T>>;

/**
 * Generator bound future
 */
class IGenf {
	protected:
		/**
		 * Proxy for bound generator.
		 * @see IGeneratorT::done 
		 */
		virtual bool done() const = 0;
		/**
		 * Resumes generation process. Proxy for bound generator.
		 * @see IGeneratorT::resume
		 * If the generator returns a future, forward it so it can be awaited.
		 * If however the generator returns a value, capture in own result and return nothing.
		 */
		virtual std::optional<AFuture> resume(const Yengine*) = 0;
	private: //State Machine controlled by the engine
		FutureState s = FutureState::Suspended;
		void setState(FutureState st){ s = st; }
	public:
		friend class Yengine;
		FutureState state() const { return s; }
		TraceCapture trace;
};

template<typename T> class IGenfT : public IGenf {
	protected:
		const Generator<T> gen;
		FutureState s = FutureState::Suspended;
		monoid<T> val;
	public:
		IGenfT(Generator<T> g) : gen(g) {}
		// Generator
		bool done() const override { return gen->done(); }
		std::optional<AFuture> resume(const Yengine* engine) override {
			return std::visit(overloaded {
				[this](monoid<T> && result) -> std::optional<AFuture> {
					val = std::move(result);
					return std::nullopt;
				},
				[this](AFuture awa) -> std::optional<AFuture> {
					return awa;
				},
			}, gen->resume(engine));
		}
		monoid<T> result(){ return std::move(val); }
};

}
