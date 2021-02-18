#pragma once

#include <variant>
#include <memory>
#include "future.hpp"

namespace yasync {

class Yengine;

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
		virtual std::variant<AFuture, movonly<T>> resume(const Yengine* engine) = 0;
};

template<typename T> using Generator = std::shared_ptr<IGeneratorT<T>>;

}
