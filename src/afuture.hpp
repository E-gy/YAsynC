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

class AFuture;
template<typename T> class Future;

class AFuture {
	public:
		using Variant = std::variant<AGenf, ANotf>;
	private:
		Variant variant;
	public:
		// inline AFuture(Variant v) : variant(v) {}
		AFuture(AGenf);
		AFuture(ANotf);
		template<typename T> inline AFuture(Future<T>);
		inline operator Variant() const { return variant; }
		bool operator==(const AFuture&) const;
		const AGenf* genf() const;
		const ANotf* notf() const;
		AGenf* genf();
		ANotf* notf();
		template<typename Visitor> decltype(auto) visit(Visitor &&) const;
		template<typename Visitor> decltype(auto) visit(Visitor &&);
		FutureState state() const;
};

template<typename T> class Future {
	public:
		using Variant = std::variant<Genf<T>, Notf<T>>;
	private:
		Variant variant;
	public:
		// inline Future(Variant v) : variant(v) {}
		Future(Genf<T>);
		Future(Notf<T>);
		template<typename V> Future(const std::shared_ptr<V>&, const IGenfT<T>&);
		template<typename V> Future(const std::shared_ptr<V>&, const INotfT<T>&);
		template<typename V> inline Future(const std::shared_ptr<V>& f) : Future(f, *f) {}
		inline operator Variant() const { return variant; }
		bool operator==(const Future&) const;
		const Genf<T>* genf() const;
		const Notf<T>* notf() const;
		Genf<T>* genf();
		Notf<T>* notf();
		template<typename Visitor> decltype(auto) visit(Visitor &&) const;
		template<typename Visitor> decltype(auto) visit(Visitor &&);
		FutureState state() const;
		movonly<T> result();
};

}

namespace std {
	template<> struct hash<yasync::AFuture> {
		inline size_t operator()(const yasync::AFuture& f) const { return std::hash<yasync::AFuture::Variant>{}(f); }
	};
	template<typename T> struct hash<yasync::Future<T>> {
		inline size_t operator()(const yasync::Future<T>& f) const { return std::hash<std::variant<yasync::Genf<T>, yasync::Notf<T>>>{}(f); }
	};
}
