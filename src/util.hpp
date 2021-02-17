#pragma once

#include <iterator>
#include <algorithm>

template<typename T> void MoveAppend(std::vector<T>& src, std::vector<T>& dst) {
	if (dst.empty()) dst = std::move(src);
	else{
		dst.reserve(dst.size() + src.size());
		std::move(std::begin(src), std::end(src), std::back_inserter(dst));
		src.clear();
	}
}
