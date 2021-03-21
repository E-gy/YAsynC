#pragma once

#include "afuture.hpp"
#include "captrace.hpp"

namespace yasync {

class INotf {
	public:
		/**
		 * Acquires the state of the future.
		 * 
		 * @returns current state of the future
		 */
		virtual FutureState state() const = 0;
		TraceCapture trace;
};

template<typename T> class INotfT : public INotf {
	public:
		/**
		 * Takes the result from the future.
		 * May return <nothing> if the future is in invalid state
		 * @returns @produces
		 */
		virtual Move<T> result() = 0;
};

}
