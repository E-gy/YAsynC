#pragma once

#include <memory>
#include "captrace.hpp"
#include <ostream>

namespace yasync {

enum class FutureState : unsigned int {
	Suspended, Queued, Running, Awaiting, Completed, Cancelled
};
std::ostream& operator<<(std::ostream& os, const FutureState& state);

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
		/**
		 * Called by the engine when [right before] the future is queued
		 */
		virtual void onQueued(){};
		TraceCapture trace;
		#ifdef _DEBUG
		virtual bool isExternal();
		#endif
};

// template<typename T> using movonly = std::unique_ptr<T>;

template<typename T> class movonly {
	std::unique_ptr<T> t;
	public:
		movonly() : t() {}
		movonly(T* pt) : t(pt) {}
		movonly(const T& vt) : t(new T(vt)) {} 
		movonly(T && vt) : t(new T(std::move(vt))) {}
		~movonly() = default;
		movonly(movonly && mov) noexcept { t = std::move(mov.t); }
		movonly& operator=(movonly && mov) noexcept { t = std::move(mov.t); return *this; }
		//no copy
		movonly(const movonly& cpy) = delete;
		movonly& operator=(const movonly& cpy) = delete;
		auto operator*(){ return std::move(t.operator*()); }
		auto operator->(){ return t.operator->(); }
};
template<> class movonly<void> {
	std::unique_ptr<void*> t;
	public:
		movonly(){}
		~movonly() = default;
		movonly(movonly &&) noexcept {}
		movonly& operator=(movonly &&) noexcept { return *this; }
		//no copy (still, yeah!)
		movonly(const movonly& cpy) = delete;
		movonly& operator=(const movonly& cpy) = delete;
};

template<typename T> class IFutureT : public IFuture {
	public:
		/**
		 * Takes the result from the future.
		 * May return <nothing> if the future is in invalid state
		 * @returns @produces
		 */
		virtual movonly<T> result() = 0;
};

using AFuture = std::shared_ptr<IFuture>;
template<typename T> using Future = std::shared_ptr<IFutureT<T>>;

}
