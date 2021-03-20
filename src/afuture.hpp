#pragma once

#include "afustate.hpp"
#include <memory>
#include "variant.hpp"

namespace yasync {

class IGenf;
template<typename T> class IGenfT;
class INotf;
template<typename T> class INotfT;

using AGenf = std::shared_ptr<IGenf>;
template<typename T> using Genf = std::shared_ptr<IGenfT<T>>;
using ANotf = std::shared_ptr<INotf>;
template<typename T> using Notf = std::shared_ptr<INotfT<T>>;

//DEPRECATED

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

//

class AFuture : public std::variant<AGenf, ANotf> {
	public:
		inline AFuture(AGenf f) : std::variant<AGenf, ANotf>(f) {}
		inline AFuture(ANotf f) : std::variant<AGenf, ANotf>(f) {}
		const AGenf* genf() const { return std::get_if<AGenf>(this); }
		const ANotf* notf() const { return std::get_if<ANotf>(this); }
		AGenf* genf(){ return std::get_if<AGenf>(this); }
		ANotf* notf(){ return std::get_if<ANotf>(this); }
		FutureState state() const;
};

template<typename T> class Future : public std::variant<Genf<T>, Notf<T>> {
	public:
		inline Future(Genf<T> f) : std::variant<Genf<T>, Notf<T>>(f) {}
		inline Future(Notf<T> f) : std::variant<Genf<T>, Notf<T>>(f) {}
		// template<typename T> inline Future(std::shared_ptr<T> f) : std::variant<Genf<T>, Notf<T>>(Genf<T>(f)) {} //generated futures don't have many reasons for being subtyped, therefor prefer notified futures for implicit convenience construction
		template<typename T> inline Future(std::shared_ptr<T> f) : std::variant<Genf<T>, Notf<T>>(Notf<T>(f)) {}
		const Genf<T>* genf() const { return std::get_if<Genf<T>>(this); }
		const Notf<T>* notf() const { return std::get_if<Notf<T>>(this); }
		Genf<T>* genf(){ return std::get_if<Genf<T>>(this); }
		Notf<T>* notf(){ return std::get_if<Notf<T>>(this); }
		FutureState state() const;
		movonly<T> result();
};

}
