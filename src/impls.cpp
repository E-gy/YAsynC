#include "impls.hpp"


bool RangeGenerator::done() const { return c >= e; }
std::variant<std::shared_ptr<FutureBase>, int> RangeGenerator::resume([[maybe_unused]] const Yengine* eng){
	return c++;
}
