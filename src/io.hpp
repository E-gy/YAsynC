#pragma once

#include <vector>
#include <string>

#include "future.hpp"
#include "engine.hpp"

#ifdef _WIN32
//https://www.drdobbs.com/cpp/multithreaded-asynchronous-io-io-comple/201202921?pgno=4
//https://gist.github.com/abdul-sami/23e1321c550dc94a9558
#include <windows.h>
#else
using fd_t = int;
#endif

namespace yasync::io {

class IOYengine;

class IAIOResource {
	public:
		virtual Future<std::vector<char>> read(unsigned bytes) = 0;
		virtual Future<void> write(const std::vector<char>& data) = 0;
};

using Resource = std::shared_ptr<IAIOResource>; 

class IOYengine {
	Yengine* engine;
	public:
		IOYengine(Yengine* e);
		~IOYengine();
		Yengine* yengine() const;
		Resource fileOpenRead(const std::string& path);
		Resource fileOpenWrite(const std::string& path);
		//TODO properly extendable for sockets
	private:
		friend class FileResource;
		void iothreadwork();
		#ifdef _WIN32
		HANDLE ioCompletionPort;
		unsigned ioThreads = 1; //IO events are dispatched by notification to the engine
		#else
		fd_t ioEpoll;
		fd_t cfdStopSend, cfdStopReceive;
		#endif
};

}
