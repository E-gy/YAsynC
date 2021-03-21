#pragma once

template<typename T> class monoid {
	T t;
	monoid() = default;
	monoid(T v) : t(v) {}
	monoid(const T& v) : t(v) {} 
	monoid(T && v) : t(std::move(v)) {}
	operator T&(){ return t; }
	T && move(){ return std::move(t); }
};
template<> struct monoid<void> {
	inline monoid() = default;
};
