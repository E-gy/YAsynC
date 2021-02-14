#include "impls.hpp"

namespace yasync {

bool RangeGenerator::done() const { return c >= e-1; }
std::variant<std::shared_ptr<FutureBase>, something<int>> RangeGenerator::resume([[maybe_unused]] const Yengine* eng){
	c++;
	return c;
}

}
