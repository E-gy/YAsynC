#pragma once

#include "agen.hpp"
#include "anot.hpp"

namespace yasync {

inline AFuture::AFuture(AGenf f) : variant(f) {}
inline AFuture::AFuture(ANotf f) : variant(f) {}
template<typename T> inline AFuture::AFuture(Future<T> f) : variant(f.visit(overloaded {
	[](const Genf<T>& f){ return Variant(std::static_pointer_cast<IGenf>(f)); },
	[](const Notf<T>& f){ return Variant(std::static_pointer_cast<INotf>(f)); },
})) {}
inline bool AFuture::operator==(const AFuture& other) const { return variant == other.variant; }
const AGenf* AFuture::genf() const { return std::get_if<AGenf>(&variant); }
const ANotf* AFuture::notf() const { return std::get_if<ANotf>(&variant); }
AGenf* AFuture::genf(){ return std::get_if<AGenf>(&variant); }
ANotf* AFuture::notf(){ return std::get_if<ANotf>(&variant); }
template<typename Visitor> decltype(auto) AFuture::visit(Visitor && v) const { return std::visit(v, variant); }
template<typename Visitor> decltype(auto) AFuture::visit(Visitor && v){ return std::visit(v, variant); }
inline FutureState AFuture::state() const {
	return std::visit(overloaded {
		[](const AGenf& f){ return f->state(); },
		[](const ANotf& f){ return f->state(); },
	}, variant);
}

template<typename T> inline Future<T>::Future(Genf<T> f) : variant(f) {}
template<typename T> inline Future<T>::Future(Notf<T> f) : variant(f) {}
template<typename T> template<typename V> inline Future<T>::Future(std::shared_ptr<V> f) : variant(std::static_pointer_cast<INotfT<T>>(f)) {}
template<typename T> inline bool Future<T>::operator==(const Future& other) const { return variant == other.variant; }
template<typename T> const Genf<T>* Future<T>::genf() const { return std::get_if<Genf<T>>(&variant); }
template<typename T> const Notf<T>* Future<T>::notf() const { return std::get_if<Notf<T>>(&variant); }
template<typename T> Genf<T>* Future<T>::genf(){ return std::get_if<Genf<T>>(&variant); }
template<typename T> Notf<T>* Future<T>::notf(){ return std::get_if<Notf<T>>(&variant); }
template<typename T> template<typename Visitor> decltype(auto) Future<T>::visit(Visitor && v) const { return std::visit(v, variant); }
template<typename T> template<typename Visitor> decltype(auto) Future<T>::visit(Visitor && v){ return std::visit(v, variant); }
template<typename T> inline FutureState Future<T>::state() const {
	return visit(overloaded {
		[](const Genf<T>& f){ return f->state(); },
		[](const Notf<T>& f){ return f->state(); },
	});
}
template<typename T> inline movonly<T> Future<T>::result(){
	return (overloaded {
		[](Genf<T>& f){ return std::move(f->result()); },
		[](Notf<T>& f){ return std::move(f->result()); },
	});
}

}
