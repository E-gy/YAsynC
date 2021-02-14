#pragma once

#include <variant>
#include <memory>

template<typename T, typename VARIANT_T>
struct isVariantMember;

template<typename T, typename... ALL_T>
struct isVariantMember<T, std::variant<ALL_T...>> : public std::disjunction<std::is_same<T, ALL_T>...> {};

/*class Somethink {
	protected:
		using CopyTrivNoExc = std::variant<char, int, long, long long, unsigned char, unsigned int, unsigned long, unsigned long long, void*>;
		std::variant<std::shared_ptr<void*>, CopyTrivNoExc> val;
		Somethink(){}
	public:
		Somethink(const Somethink& cpy) = delete;
		Somethink(Somethink&& mv) noexcept {
			val = mv.val;
			mv.val = CopyTrivNoExc(0);
		}
};*/

/**
 * Memory speed optimized type-safe box, with safe* type erasure capabilities. 
 * WARNING: Do not use the move constructor after type erasure. Very bad things will happen.
 * TODO: delete move constructor for type-erased self.
 */
template<typename T> class something {
	protected:
		using CopyTrivNoExc = std::variant<char, int, long, long long, unsigned char, unsigned int, unsigned long, unsigned long long, void*>;
		using PT = T*;
		using RT = typename std::conditional<isVariantMember<T, CopyTrivNoExc>::value, T, PT>::type;
		std::variant<PT, CopyTrivNoExc> val;
	public:
		something(const T& v) noexcept {
			if constexpr (isVariantMember<T, CopyTrivNoExc>::value) val = CopyTrivNoExc(v);
			else val = PT(new T(v));
		}
		something(const something& cpy) noexcept {
			if constexpr (isVariantMember<T, CopyTrivNoExc>::value) val = CopyTrivNoExc(cpy.get());
			else val = new T(cpy.get());
		}
		// something(const something& cpy) = delete;
		something(something&& mv) noexcept {
			val = mv.val;
			/*if constexpr (isVariantMember<T, CopyTrivNoExc>::value) mv.val = nullptr;
			else*/
			mv.val = CopyTrivNoExc(0);
		}
		~something() noexcept {
			// if constexpr (!isVariantMember<T, CopyTrivNoExc>::value) if(auto pt = std::get<PT>(val)) delete pt;
			if constexpr (!isVariantMember<T, CopyTrivNoExc>::value) if(auto pt = std::get_if<PT>(&val)) delete *pt;
		}
		T get() const noexcept {
			if constexpr (isVariantMember<T, CopyTrivNoExc>::value) return std::get<T>(std::get<CopyTrivNoExc>(val));
			else return *std::get<PT>(val);
		}
		RT getr() const noexcept {
			if constexpr (isVariantMember<T, CopyTrivNoExc>::value) return std::get<T>(std::get<CopyTrivNoExc>(val));
			else return std::get<PT>(val);
		}
		operator T() const noexcept { return get(); }
};
