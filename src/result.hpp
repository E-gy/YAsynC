#pragma once

#include <variant>
#include <optional>

template<typename S, typename E> class result {
	std::variant<S, E> res;
	protected:
		result(const std::variant<S, E>& r) : res(r) {}
		result(std::variant<S, E> && r) : res(r) {}
	public:
		using OK = S;
		using ERR = E;
		result(const S& ok) : res(ok) {}
		result(const E& err) : res(err) {}
		result(S && ok) : res(ok) {}
		result(E && err) : res(err) {}
		bool isOk() const { return res.index() == 0; }
		bool isErr() const { return res.index() == 1; }
		const S* ok() const { return std::get_if<S>(&res); }
		const E* err() const { return std::get_if<E>(&res); }
		std::optional<S> okOpt() const { if(auto r = std::get_if<S>(&res)) return *r; else return std::nullopt; }
		std::optional<E> errOpt() const { if(auto r = std::get_if<E>(&res)) return *r; else return std::nullopt; }
		operator bool() const { return isOk(); }
		template<typename U, typename F> result<U, E> mapOk_(F f) const { if(auto r = std::get_if<S>(&res)) return result<U, E>::Ok(f(*r)); else return result<U, E>::Err(*err()); }
		template<typename V, typename F> result<S, V> mapError_(F f) const { if(auto r = std::get_if<E>(&res)) return result<S, V>::Err(f(*r)); else return result<S, V>::Ok(*ok()); }
		template<typename F> auto mapOk(F f) const {
			using U = std::decay_t<decltype(f(*std::get_if<S>(&res)))>;
			return mapOk_<U, F>(f);
		}
		template<typename F> auto mapError(F f) const {
			using V = std::decay_t<decltype(f(*std::get_if<E>(&res)))>;
			return mapError_<V, F>(f);
		}
	public:
		static result Ok(const S& ok){ return result(ok); }
		static result Err(const E& err){ return result(err); }
		static result Ok(S && ok){ return result(ok); }
		static result Err(E && err){ return result(err); }
};
template<typename T> class result<T, T> {
	bool okay;
	T thing;
	protected:
		result(const bool& ok, const T& t) : okay(ok), thing(t) {}
		result(const bool& ok, T && t) : okay(ok), thing(t) {}
	public:
		using OK = T;
		using ERR = T;
		bool isOk() const { return okay; }
		bool isErr() const { return !okay; }
		const T* ok() const { return isOk() ? &thing : nullptr; }
		const T* err() const { return isErr() ? &thing : nullptr; }
		std::optional<T> okOpt() const { if(isOk()) return thing; else return std::nullopt; }
		std::optional<T> errOpt() const { if(isErr()) return thing; else return std::nullopt; }
		operator bool() const { return isOk(); }
		template<typename U, typename F> result<U, T> mapOk_(F f) const { if(isOk()) return result<U, T>::Ok(f(thing)); else return result<U, T>::Err(*err()); }
		template<typename V, typename F> result<T, V> mapError_(F f) const { if(isErr()) return result<T, V>::Err(f(thing)); else return result<T, V>::Ok(*ok()); }
		template<typename F> auto mapOk(F f) const {
			using U = std::decay_t<decltype(f(thing))>;
			return mapOk_<U, F>(f);
		}
		template<typename F> auto mapError(F f) const {
			using V = std::decay_t<decltype(f(thing))>;
			return mapError_<V, F>(f);
		}
	public:
		static result Ok(const T& ok){ return result(true, ok); }
		static result Err(const T& err){ return result(false, err); }
		static result Ok(T && ok){ return result(true, ok); }
		static result Err(T && err){ return result(false, err); }
};
template<typename S> class result<S, void> {
	std::optional<S> okay;
	public:
		using OK = S;
		using ERR = void;
		result(const S& ok) : okay(ok) {}
		result() : okay() {}
		bool isOk() const { return okay.has_value(); }
		bool isErr() const { return !okay.has_value(); }
		const S* ok() const { return okay.has_value() ? okay.operator->() : nullptr; }
		std::optional<S> okOpt() const { return okay; }
		template<typename U, typename F> result<U, void> mapOk_(F f) const { if(auto r = ok()) return result<U, void>::Ok(f(*r)); else return result<U, void>::Err(); }
		template<typename V, typename F> result<S, V> mapError_(F f) const { if(isErr()) return result<S, V>::Err(f()); else return result<S, V>::Ok(*ok()); }
		template<typename F> auto mapOk(F f) const {
			using U = std::decay_t<decltype(f(*okay))>;
			return mapOk_<U, F>(f);
		}
		template<typename F> auto mapError(F f) const {
			using V = std::decay_t<decltype(f())>;
			return mapError_<V, F>(f);
		}
	public:
		static result Ok(const S& ok){ return result(ok); }
		static result Ok(S && ok){ return result(ok); }
		static result Err(){ return result(); }
};
template<typename E> class result<void, E> {
	std::optional<E> error;
	public:
		using OK = void;
		using ERR = E;
		result() : error() {}
		result(const E& e) : error(e) {}
		bool isOk() const { return !error.has_value(); }
		bool isErr() const { return error.has_value(); }
		const E* err() const { return error.has_value() ? error.operator->() : nullptr; }
		std::optional<E> errOpt() const { return error; }
		template<typename U, typename F> result<U, E> mapOk_(F f) const { if(isOk()) return result<U, E>::Ok(f()); else return result<U, E>::Err(*err()); }
		template<typename V, typename F> result<void, V> mapError_(F f) const { if(auto r = err()) return result<void, V>::Err(f(*r)); else return result<void, V>::Ok(); }
		template<typename F> auto mapOk(F f) const {
			using U = std::decay_t<decltype(f())>;
			return mapOk_<U, F>(f);
		}
		template<typename F> auto mapError(F f) const {
			using V = std::decay_t<decltype(f(*err))>;
			return mapError_<V, F>(f);
		}
	public:
		static result Ok(){ return result(); }
		static result Err(const E& err){ return result(err); }
		static result Err(E && err){ return result(err); }
};
template<> class result<void, void> {
	bool okay;
	protected:
		result(const bool& ok) : okay(ok) {}
	public:
		using OK = void;
		using ERR = void;
		bool isOk() const { return okay; }
		bool isErr() const { return !okay; }
		operator bool() const { return isOk(); }
		template<typename U, typename F> result<U, void> mapOk_(F f) const { if(isOk()) return result<U, void>::Ok(f()); else return result<U, void>::Err(); }
		template<typename V, typename F> result<void, V> mapError_(F f) const { if(isErr()) return result<void, V>::Err(f()); else return result<void, V>::Ok(); }
		template<typename F> auto mapOk(F f) const {
			using U = std::decay_t<decltype(f())>;
			return mapOk_<U, F>(f);
		}
		template<typename F> auto mapError(F f) const {
			using V = std::decay_t<decltype(f())>;
			return mapError_<V, F>(f);
		}
	public:
		static result Ok(){ return result(true); }
		static result Err(){ return result(false); }
};
