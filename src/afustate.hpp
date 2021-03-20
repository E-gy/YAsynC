#pragma once

#include <ostream>

namespace yasync {

enum class FutureState : unsigned int {
	Suspended, Queued, Running, Awaiting, Completed, Cancelled
};
inline std::ostream& operator<<(std::ostream& os, const FutureState& state){
	switch(state){
		case FutureState::Suspended: return os << "Suspended";
		case FutureState::Queued: return os << "Queued";
		case FutureState::Running: return os << "Running";
		case FutureState::Awaiting: return os << "Awaiting";
		case FutureState::Completed: return os << "Completed";
		case FutureState::Cancelled: return os << "Cancelled";
		default: return os << "<Invalid State>";
	}
}

}
