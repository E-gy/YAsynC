#include "daemons.hpp"

Daemons::Daemons(){}
Daemons* Daemons::INSTANCE(){
	static Daemons daemons;
	return &daemons;
}
void Daemons::waitForEveryoneToStop(){
	std::unique_lock lok(Daemons::INSTANCE()->runningLock);
	while(Daemons::INSTANCE()->runningDaemons > 0) Daemons::INSTANCE()->everyoneStopped.wait(lok);
}

Daemons::Daemon::Daemon(TraceCapture c) : creator(c) {
	std::unique_lock lok(Daemons::INSTANCE()->runningLock);
	Daemons::INSTANCE()->runningDaemons++;
}
Daemons::Daemon::~Daemon(){
	std::unique_lock lok(Daemons::INSTANCE()->runningLock);
	Daemons::INSTANCE()->runningDaemons--;
	if(!Daemons::INSTANCE()->runningDaemons) Daemons::INSTANCE()->everyoneStopped.notify_all();
}