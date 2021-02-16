#pragma once

#include <vector>
#include <string>

#include "future.hpp"
#include "engine.hpp"

using fd_t = int;
#ifdef _WIN32
//https://www.drdobbs.com/cpp/multithreaded-asynchronous-io-io-comple/201202921?pgno=4
//https://gist.github.com/abdul-sami/23e1321c550dc94a9558
#include <windows.h>
using ResourceHandle = HANDLE;
#else
using ResourceHandle = fd_t;
#endif

namespace yasync::io {

class IOYengine;

#ifdef _WIN32
struct IOCompletionInfo {
	BOOL status;
	DWORD transferred;
	DWORD lerr;
};
#else
using IOCompletionInfo = int;
#endif

class IAIOResource {
	virtual void notify(IOCompletionInfo inf) = 0;
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
		/**
		 * Opens asynchronous resource on the handle.
		 * @param r @consumes
		 * @returns async resource
		 */
		Resource taek(ResourceHandle r);
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
