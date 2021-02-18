#pragma once

#include <optional>
#include <memory>
#include "something.hpp"

namespace yasync {

enum class FutureState : unsigned int {
	Suspended, Queued, Running, Awaiting, Completed
};

class IFuture {
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

//TODO FIXME someone f'd up a lot of things. Futures' boxed containers cause waay too many copies. That's bad for large data.

template<typename T> class IFutureT : public IFuture {
	public:
		/**
		 * The future owns the result, always.
		 * This is reference accessor.
		 * @returns @ref
		 */
		virtual std::optional<something<T>> result() = 0;
};

using AFuture = std::shared_ptr<IFuture>;
template<typename T> using Future = std::shared_ptr<IFutureT<T>>;

}
