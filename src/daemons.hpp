#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include "captrace.hpp"

class Daemons final {
	std::mutex runningLock;
	int runningDaemons = 0;
	std::condition_variable everyoneStopped;
	Daemons();
	class Daemon {
		friend class Daemons;
		TraceCapture creator;
		Daemon(TraceCapture c);
		~Daemon();
	};
	friend class Daemon;
	public:
		static Daemons* INSTANCE();
		template<typename F, typename... Args> static void launch(F f, Args... args){
			TraceCapture created;
			std::thread demon([=](Args... args){
				Daemon capt(created);
				f(args...);
			}, args...);
			demon.detach();
		}
		static void waitForEveryoneToStop();
};
