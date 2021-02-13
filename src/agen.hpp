#pragma once

#include <optional>
#include <memory>

class Yengine;
class FutureBase;

/**
 * Generic asynchronous generator interface.
 * - `done` must always indicate whether the generation has completed (even prior to initial `resume` call)
 * - the generator can transition only from `!done` into `done` state and only on a producing [ret none] `resume`.
 * - `resume` will not be called again after the generator goes into `done` state
 * - `ret` will not be invoked before the initial `resume` call chain completes (`resume` calls until one produces).
 * - `ret` must hold the last return value, even after the generator is `done`, for as long as the generator exists
 */
template<typename R/*, typename... P*/> class AGenerator {
	public:
		// virtual AGenerator(P... args) = 0;
		/**
		 * @returns whether the generation has finished
		 */
		virtual bool done() const = 0;
		/**
		 * @returns @produces the [lastest] produced value, None if the generation hasn't started
		 */
		virtual std::optional<R> ret() const = 0;
		/**
		 * Resumes the generation process.
		 * Invoked as initialization, prior to any other invocations.
		 * Returning a future means that the generator is waiting for that future's completion, and this function will be invoked again once it completes.
		 * Returning none means that the generator produced a value (and potentially finished the process), and this function will be invoked again iff the generator is not done when the next value is requested.
		 * 
		 * @param engine @ref async engine to launch tasks in parallel
		 * @returns @produces None if the next value is ready, the future this generator is awaiting for otherwise
		 */
		virtual std::optional<std::shared_ptr<FutureBase>> resume(const Yengine* engine) = 0;
};
