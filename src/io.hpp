#pragma once

#include <vector>
#include <string>

#include "future.hpp"
#include "engine.hpp"
#include "result.hpp"

using fd_t = int;
#ifdef _WIN32
//https://www.drdobbs.com/cpp/multithreaded-asynchronous-io-io-comple/201202921?pgno=4
//https://gist.github.com/abdul-sami/23e1321c550dc94a9558
#include <windows.h>
using ResourceHandle = HANDLE;
using syserr_t = DWORD;
#else
using ResourceHandle = fd_t;
using syserr_t = int;
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

class IResource {
	friend class IOYengine;
	/**
	 * Used by the engine to notify the resource of completion of current IO operation
	 */
	virtual void notify(IOCompletionInfo inf) = 0;
	#ifdef _WIN32
	struct Overlapped {
		OVERLAPPED overlapped;
		IResource* resource;
	};
	Overlapped _overlapped;
	#endif
	public:
		#ifdef _WIN32
		IResource() : _overlapped({{}, this}) {}
		OVERLAPPED* overlapped(){ return &_overlapped.overlapped; }
		#else
		IResource(){}
		#endif
};

class IAIOResource : public IResource {
	public:
		virtual Future<result<std::vector<char>, std::string>> read(unsigned bytes) = 0;
		virtual Future<result<void, std::string>> write(const std::vector<char>& data) = 0;
};

using IOResource = std::shared_ptr<IAIOResource>; 

class IOYengine {
	public:
		Yengine* const engine;
		IOYengine(Yengine* e);
		~IOYengine();
		// result<void, int> iocplReg(ResourceHandle r, bool rearm); as much as we'd love to do that, there simply waay to many differences between IOCompletion and EPoll
		//so let's make platform specific internals public instead ¯\_(ツ)_/¯
		#ifdef _WIN32
		HANDLE const ioCompletionPort;
		#else
		fd_t const ioEpoll;
		#endif
		/**
		 * Opens asynchronous resource on the handle.
		 * @param r @consumes
		 * @returns async resource
		 */
		IOResource taek(ResourceHandle r);
		result<IOResource, std::string> fileOpenRead(const std::string& path);
		result<IOResource, std::string> fileOpenWrite(const std::string& path);
		//TODO properly extendable for sockets
	private:
		friend class IResource;
		void iothreadwork();
		#ifdef _WIN32
		static constexpr unsigned ioThreads = 1; //IO events are dispatched by notification to the engine
		#else
		fd_t cfdStopSend, cfdStopReceive;
		#endif
};

}
