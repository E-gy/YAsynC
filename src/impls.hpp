#pragma once

#include "agen.hpp"
#include "future.hpp"

class RangeGenerator : public AGenerator<int> {
	int s, e;
	int c;
	public:
		RangeGenerator(int start, int end) : s(start), e(end), c(s-1) {}
		bool done() const;
		std::variant<std::shared_ptr<FutureBase>, int> resume(const Yengine* eng);
};

class Yengine;

template<typename T> std::shared_ptr<Future<T>> asyncSleep(Yengine* engine, unsigned ms, T ret);
