#pragma once

#include <variant>
#include <optional>

template<typename S, typename E> class result {
	std::variant<S, E> res;
	public:
		result(const S& ok) : res(ok) {}
		result(const E& err) : res(err) {}
		bool isOk() const { return res.index() == 0; }
		bool isError() const { return res.index() == 1; }
		std::optional<S> ok() const { if(auto r = std::get_if<S>(&res)) return *r; else return std::nullopt; }
		std::optional<E> error() const { if(auto r = std::get_if<E>(&res)) return *r; else return std::nullopt; }
};
template<typename S> class result<S, void> {
	std::optional<S> okay;
	public:
		result(const S& ok) : okay(ok) {}
		result() : okay() {}
		bool isOk() const { return okay.has_value(); }
		bool isError() const { return !okay.has_value(); }
		std::optional<S> ok() const { return okay; }
};
template<typename E> class result<void, E> {
	std::optional<E> err;
	public:
		result() : err() {}
		result(const E& e) : err(e) {}
		bool isOk() const { return !err.has_value(); }
		bool isError() const { return err.has_value(); }
		std::optional<E> error() const { return err; }
};
template<typename S, typename E> result<S, E> ROk(const S& ok){ return result<S, E>(ok); }
template<typename E> result<void, E> ROk(){ return result<void, E>(); }
template<typename S, typename E> result<S, E> RError(const E& err){ return result<S, E>(err); }
template<typename S> result<S, void> RError(){ return result<S, void>(); }
