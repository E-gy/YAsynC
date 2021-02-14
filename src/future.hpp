#pragma once

#include <optional>
#include "something.hpp"

namespace yasync {

enum class FutureState : unsigned int {
	Suspended, Queued, Running, Awaiting, Completed
};

class FutureBase {
	public:
		/**
		 * Acquires the state of the future.
		 * Most states are "reserved" for internal use - linked to the generator's state.
		 * The 2 that aren't, and only ones that actually make sense for any non-engine future are `Running` and `Completed`.
		 * 
		 * @returns current state of the future
		 */
		virtual FutureState state() = 0;
};

template<typename T> class Future : public FutureBase {
	public:
		/**
		 * The future owns the result, always.
		 * This is reference accessor.
		 * @returns @ref
		 */
		virtual std::optional<something<T>> result() = 0;
};

}
