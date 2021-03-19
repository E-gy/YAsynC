#include "impls.hpp"

namespace yasync {

template<> void blawait<void>(Yengine* engine, Future<void> f){
	bool t = false;
	std::mutex synch;
	std::condition_variable cvDone;
	engine <<= f >> [&](){
		std::unique_lock lok(synch);
		t = true;
		cvDone.notify_one();
	};
	std::unique_lock lok(synch);
	while(!t) cvDone.wait(lok);
}

}
