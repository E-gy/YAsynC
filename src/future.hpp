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
inline const AGenf* AFuture::genf() const { return std::get_if<AGenf>(&variant); }
inline const ANotf* AFuture::notf() const { return std::get_if<ANotf>(&variant); }
inline AGenf* AFuture::genf(){ return std::get_if<AGenf>(&variant); }
inline ANotf* AFuture::notf(){ return std::get_if<ANotf>(&variant); }
template<typename Visitor> inline decltype(auto) AFuture::visit(Visitor && v) const { return std::visit(v, variant); }
template<typename Visitor> inline decltype(auto) AFuture::visit(Visitor && v){ return std::visit(v, variant); }
inline FutureState AFuture::state() const {
	return std::visit(overloaded {
		[](const AGenf& f){ return f->state(); },
		[](const ANotf& f){ return f->state(); },
	}, variant);
}

template<typename T> inline Future<T>::Future(Genf<T> f) : variant(f) {}
template<typename T> inline Future<T>::Future(Notf<T> f) : variant(f) {}
template<typename T> template<typename V> inline Future<T>::Future(const std::shared_ptr<V>& f, const IGenfT<T>&) : variant(std::static_pointer_cast<IGenfT<T>>(f)) {}
template<typename T> template<typename V> inline Future<T>::Future(const std::shared_ptr<V>& f, const INotfT<T>&) : variant(std::static_pointer_cast<INotfT<T>>(f)) {}
template<typename T> inline bool Future<T>::operator==(const Future& other) const { return variant == other.variant; }
template<typename T> inline const Genf<T>* Future<T>::genf() const { return std::get_if<Genf<T>>(&variant); }
template<typename T> inline const Notf<T>* Future<T>::notf() const { return std::get_if<Notf<T>>(&variant); }
template<typename T> inline Genf<T>* Future<T>::genf(){ return std::get_if<Genf<T>>(&variant); }
template<typename T> inline Notf<T>* Future<T>::notf(){ return std::get_if<Notf<T>>(&variant); }
template<typename T> template<typename Visitor> inline decltype(auto) Future<T>::visit(Visitor && v) const { return std::visit(v, variant); }
template<typename T> template<typename Visitor> inline decltype(auto) Future<T>::visit(Visitor && v){ return std::visit(v, variant); }
template<typename T> inline FutureState Future<T>::state() const {
	return visit(overloaded {
		[](const Genf<T>& f){ return f->state(); },
		[](const Notf<T>& f){ return f->state(); },
	});
}
template<typename T> inline Move<T> Future<T>::result(){
	return visit(overloaded {
		[](Genf<T>& f){ return f->result(); },
		[](Notf<T>& f){ return f->result(); },
	}).move();
}
template<> inline Move<void> Future<void>::result(){}

}
