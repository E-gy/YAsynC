#pragma once

#include "agen.hpp"
#include "anot.hpp"

namespace yasync {

inline FutureState AFuture::state() const {
	return std::visit(overloaded {
		[](const AGenf& f){ return f->state(); },
		[](const ANotf& f){ return f->state(); },
	}, *this);
}
template<typename T> inline FutureState Future<T>::state() const {
	return std::visit(overloaded {
		[](const Genf<T>& f){ return f->state(); },
		[](const Notf<T>& f){ return f->state(); },
	}, *this);
}
template<typename T> inline movonly<T> Future<T>::result(){
	return std::visit(overloaded {
		[](Genf<T>& f){ return std::move(f->result()); },
		[](Notf<T>& f){ return std::move(f->result()); },
	}, *this);
}

}
