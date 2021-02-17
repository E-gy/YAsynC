#pragma once

#include <variant>
#include <optional>

template<typename S, typename E> class result;

template<typename S, typename E> result<S, E> ROk(const S& ok){ return result<S, E>(ok); }
template<typename E> result<void, E> ROk(){ return result<void, E>(); }
template<typename S, typename E> result<S, E> RError(const E& err){ return result<S, E>(err); }
template<typename S> result<S, void> RError(){ return result<S, void>(); }

template<typename S, typename E> class result {
	std::variant<S, E> res;
	public:
		result(const S& ok) : res(ok) {}
		result(const E& err) : res(err) {}
		result(S && ok) : res(ok) {}
		result(E && err) : res(err) {}
		bool isOk() const { return res.index() == 0; }
		bool isError() const { return res.index() == 1; }
		std::optional<S> ok() const { if(auto r = std::get_if<S>(&res)) return *r; else return std::nullopt; }
		std::optional<E> error() const { if(auto r = std::get_if<E>(&res)) return *r; else return std::nullopt; }
		operator bool() const { return isOk(); }
		template<typename U, typename F> result<U, E> mapOk_(F f) const { if(auto r = std::get_if<S>(&res)) return ROk<U, E>(f(*r)); else return RError<U, E>(*error()); }
		template<typename V, typename F> result<S, V> mapError_(F f) const { if(auto r = std::get_if<E>(&res)) return RError<S, V>(f(*r)); else return ROk<S, V>(*ok()); }
		template<typename F> auto mapOk(F f) const {
			using U = std::decay_t<decltype(f(*std::get_if<S>(&res)))>;
			return mapOk_<U, F>(f);
		}
		template<typename F> auto mapError(F f) const {
			using V = std::decay_t<decltype(f(*std::get_if<E>(&res)))>;
			return mapError_<V, F>(f);
		}
};
template<typename S> class result<S, void> {
	std::optional<S> okay;
	public:
		result(const S& ok) : okay(ok) {}
		result() : okay() {}
		bool isOk() const { return okay.has_value(); }
		bool isError() const { return !okay.has_value(); }
		std::optional<S> ok() const { return okay; }
		template<typename U, typename F> result<U, void> mapOk_(F f) const { if(auto r = ok()) return ROk<U, void>(f(*r)); else return RError<U, void>(); }
		template<typename V, typename F> result<S, V> mapError_(F f) const { if(isError()) return RError<S, V>(f()); else return ROk<S, V>(*ok()); }
		template<typename F> auto mapOk(F f) const {
			using U = std::decay_t<decltype(f(*okay))>;
			return mapOk_<U, F>(f);
		}
		template<typename F> auto mapError(F f) const {
			using V = std::decay_t<decltype(f())>;
			return mapError_<V, F>(f);
		}
};
template<typename E> class result<void, E> {
	std::optional<E> err;
	public:
		result() : err() {}
		result(const E& e) : err(e) {}
		bool isOk() const { return !err.has_value(); }
		bool isError() const { return err.has_value(); }
		std::optional<E> error() const { return err; }
		template<typename U, typename F> result<U, E> mapOk_(F f) const { if(isOk()) return ROk<U, E>(f()); else return RError<U, E>(*error()); }
		template<typename V, typename F> result<void, V> mapError_(F f) const { if(auto r = error()) return RError<void, V>(f(*r)); else return ROk<void, V>(); }
		template<typename F> auto mapOk(F f) const {
			using U = std::decay_t<decltype(f())>;
			return mapOk_<U, F>(f);
		}
		template<typename F> auto mapError(F f) const {
			using V = std::decay_t<decltype(f(*err))>;
			return mapError_<V, F>(f);
		}
};
