#pragma once

template<typename T> struct monoid {
	T t;
	monoid() = default;
	monoid(const T& v) : t(std::forward<T>(v)) {} 
	monoid(T && v) : t(std::forward<T>(v)) {}
	// operator T&(){ return t; } Disabled due to potential to wreak havoc in innocent use cases.
	T && move(){ return std::move(t); }
};
template<> struct monoid<void> {
	inline monoid() = default;
};
