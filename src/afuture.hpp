#pragma once

#include "afustate.hpp"
#include <memory>
#include <optional>
#include "variant.hpp"
#include "monoid.hpp"

namespace yasync {

class IGenf;
template<typename T> class IGenfT;
class INotf;
template<typename T> class INotfT;

using AGenf = std::shared_ptr<IGenf>;
template<typename T> using Genf = std::shared_ptr<IGenfT<T>>;
using ANotf = std::shared_ptr<INotf>;
template<typename T> using Notf = std::shared_ptr<INotfT<T>>;

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
		T result();
};

/**
 * A very special type. Effectively a dedicated optional.
 * When used in conjuctions with pipelining futures allows yielding of nothing (and in most cases act as indication of doneness).
 */
template<typename T> struct Maybe {
	std::optional<T> t;
	Maybe() = default;
	Maybe(const T& v) : t(std::forward<T>(v)) {}
	Maybe(T && v) : t(std::forward<T>(v)) {}
	inline bool isSome() const { return t.has_value(); }
	inline operator bool() const { return isSome(); }
	inline T operator*(){ return std::move(*t); }
};
template<> struct Maybe<void> {
	bool has;
	Maybe() = default;
	Maybe(bool h) : has(h){}
	inline bool isSome() const { return has; }
	inline operator bool() const { return isSome(); }
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
