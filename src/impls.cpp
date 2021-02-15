#include "impls.hpp"

namespace yasync {

bool RangeGenerator::done() const { return c >= e-1; }
std::variant<AFuture, something<int>> RangeGenerator::resume([[maybe_unused]] const Yengine* eng){
	c++;
	return c;
}

}
