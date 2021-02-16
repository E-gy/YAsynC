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

class IAIOResource;
using IOResource = std::shared_ptr<IAIOResource>; 

class IAIOResource : public IResource {
	/**
	 * Optimal [IO data] Block Size
	 */
	static constexpr size_t OBS = 4096;
	using ReadResult = result<std::vector<char>, std::string>;
	using WriteResult = result<void, std::string>;
	protected:
		IAIOResource(IOYengine* e) : engine(e){}
		/**
		 * Reads up to the number of bytes requested, or EOD (if unspecified), from the resource.
		 * @param bytes number of bytes to read, 0 for unlimited
		 * @returns result of the read
		 */
		virtual Future<ReadResult> _read(size_t bytes = 0) = 0;
		/**
		 * Writes the data to the resource.
		 * @param data data to write
		 * @returns result of the write
		 */
		virtual Future<WriteResult> _write(std::vector<char>&& data) = 0;
	private:
		std::vector<char> readbuff;
	public:
		IOYengine* const engine;
		//L1
		/**
		 * Reads until EOD.
		 */
		template<typename T> Future<result<T, std::string>> read();
		/**
		 * Reads up to number of bytes, or EOD.
		 */
		template<typename T> Future<result<T, std::string>> read(size_t upto);	
		/**
		 * Reads until reaching the pattern. Pattern is included in and is the last sequence of the result.
		 */
		template<typename T, typename PatIt> Future<result<T, std::string>> read(PatIt patBegin, PatIt patEnd);

		/**
		 * Writes data
		 */
		template<typename DataP> Future<WriteResult> write(DataP data, size_t dataSize);
		/**
		 * Writes data
		 */
		template<typename DataIt> Future<WriteResult> write(DataIt dataBegin, DataIt dataEnd);
		/**
		 * Writes data
		 */
		template<typename Range> Future<WriteResult> write(const Range& dataRange);
		//L2
		/**
		 * (Lazy?) Stream-like writer.
		 * The data is accumulated locally.
		 * The data can be flushed at any intermediate point.
		 * Letting writer go incurs the last flush and the eod can be fullfilled.
		 */
		class Writer {
			IOResource resource;
			public:
				/**
				 * Provides a future that is resolved when the writer finished _all_ writing.
				 */
				Future<WriteResult> eod();
				/**
				 * Flushes intermediate data [_proactively_!].
				 * The writer can still be used after the flush.
				 */
				Future<WriteResult> flush();
				template<typename DataIt> auto write(DataIt dataBegin, DataIt dataEnd);
				template<typename Data> auto operator<<(Data d);
		};
		/**
		 * Creates a new [lazy] (text) writer for the resource
		 */
		Writer writer();
};

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
