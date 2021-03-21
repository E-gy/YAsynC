#pragma once

template<typename T> struct monoid {
	T t;
	monoid() = default;
	monoid(const T& v) : t(std::forward(v)) {} 
	monoid(T && v) : t(std::forward(v)) {}
	operator T&(){ return t; }
	T && move(){ return std::move(t); }
};
template<> struct monoid<void> {
	inline monoid() = default;
};
