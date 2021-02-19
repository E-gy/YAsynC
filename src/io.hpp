#pragma once

#include <vector>
#include <string>

#include "future.hpp"
#include "engine.hpp"
#include "result.hpp"
#include "util.hpp"
#include "impls.hpp"
#include "syserr.hpp"
#include <sstream>

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

/**
 * Represents a resource whose state is managed (externally).
 * A default implementation would simply close the handle for example.
 * A reuseable resource might repuporse it instead.
 * ...
 */
class IHandledResource {
	public:
		const ResourceHandle rh; //gotta go fast
		IHandledResource(ResourceHandle r);
		virtual ~IHandledResource() = 0;
};
using HandledResource = std::unique_ptr<IHandledResource>;

#ifdef _WIN32
struct IOCompletionInfo {
	BOOL status;
	DWORD transferred;
	DWORD lerr;
};
constexpr unsigned COMPLETION_KEY_SHUTDOWN = 1;
constexpr unsigned COMPLETION_KEY_IO = 2;
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

template<typename T> auto mapVecToT(){
	if constexpr (std::is_same<T, std::vector<char>>::value) return [](auto r){ return r; };
	else return [](auto rr){ return rr.mapOk([](auto v){ return T(v.begin(), v.end()); }); };
}

class IAIOResource : public IResource {
	protected:
		IAIOResource(IOYengine* e) : engine(e){}
		std::weak_ptr<IAIOResource> slf;
		auto setSelf(std::shared_ptr<IAIOResource> self){ return slf = self; }
		using ReadResult = result<std::vector<char>, std::string>;
		/**
		 * Reads _at least_ the number of bytes requested, or until EOD is reached.
		 * If no bytes are requested, read until EOD.
		 * Effectively, a request of single byte read is equivalent to the read of one optimal buffer unit of associated resource.
		 * Unbuffered, all and only data acquired from underlying resource _including data beyond requested size_ must be returned.
		 * @param bytes number of bytes to read, 0 for unlimited
		 * @returns result of the read
		 */
		virtual Future<ReadResult> _read(size_t bytes = 0) = 0;
		using WriteResult = result<void, std::string>;
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
		template<typename T> Future<result<T, std::string>> read(){
			return read<std::vector<char>>() >> mapVecToT<T>();
		}
		/**
		 * Reads up to number of bytes, or EOD.
		 */
		template<typename T> Future<result<T, std::string>> read(size_t upto){
			return read<std::vector<char>>(upto) >> mapVecToT<T>();
		}
		template<typename PatIt> Future<ReadResult> read_(const PatIt& patBegin, const PatIt& patEnd);
		/**
		 * Reads until reaching the pattern. Pattern is included in and is the last sequence of the result.
		 */
		template<typename T, typename PatIt> Future<result<T, std::string>> read(const PatIt& patBegin, const PatIt& patEnd){
			return read_<PatIt>(patBegin, patEnd) >> mapVecToT<T>();
		}
		template<typename T> Future<result<T, std::string>> read(const std::string& pattern){
			return read<T>(pattern.begin(), pattern.end());
		}

		/**
		 * Writes data
		 */
		template<typename DataP> Future<WriteResult> write(DataP data, size_t dataSize){
			return write(std::vector<char>(data, data+dataSize));
		}
		/**
		 * Writes data
		 */
		template<typename DataIt> Future<WriteResult> write(DataIt dataBegin, DataIt dataEnd){
			return write(std::vector<char>(dataBegin, dataEnd));
		}
		/**
		 * Writes data
		 */
		template<typename Range> Future<WriteResult> write(const Range& dataRange){
			return write(std::vector<char>(dataRange.begin(), dataRange.end()));
		}
		template<typename Range> Future<WriteResult> write(Range && dataRange){
			return write(std::vector<char>(dataRange.begin(), dataRange.end()));
		}
		//L2
		/**
		 * (Lazy?) Stream-like writer.
		 * The data is accumulated locally.
		 * The data can be flushed at any intermediate point.
		 * Letting writer go incurs the last flush and the eod can be fullfilled.
		 */
		class Writer {
			IOResource resource;
			std::vector<char> buffer;
			std::shared_ptr<OutsideFuture<WriteResult>> eodnot;
			Future<WriteResult> lflush;
			public:
				Writer(IOResource resource);
				~Writer();
				Writer(const Writer& cpy) = delete;
				Writer(Writer && mv) = delete;
				/**
				 * Provides a future that is resolved when the writer finished _all_ writing.
				 */
				Future<WriteResult> eod() const;
				/**
				 * Flushes intermediate data [_proactively_!].
				 * The writer can still be used after the flush.
				 */
				Future<WriteResult> flush();
				template<typename DataIt> auto write(DataIt dataBegin, DataIt dataEnd){
					buffer.insert(buffer.end(), dataBegin, dataEnd);
					return this;
				}
				template<typename DataRange> auto write(DataRange range){
					return write(range.begin(), range.end());
				}
				template<typename Data> auto operator<<(Data d){
					std::ostringstream buff;
					buff << d;
					write(buff.str());
					return this;
				}
		};
		/**
		 * Creates a new [lazy] (text) writer for the resource
		 */
		std::shared_ptr<Writer> writer();
};

template<> Future<IAIOResource::ReadResult> IAIOResource::read<std::vector<char>>();
template<> Future<IAIOResource::ReadResult> IAIOResource::read<std::vector<char>>(size_t upto);

template<typename PatIt> Future<IAIOResource::ReadResult> IAIOResource::read_(const PatIt& patBegin, const PatIt& patEnd){
	for(size_t i = 0; i < readbuff.size(); i++){
		auto pat = patBegin;
		size_t j = i;
		for(; j < readbuff.size() && pat != patEnd; j++, pat++) if(readbuff[j] != *pat) break;
		if(pat == patEnd) return read<std::vector<char>>(j);
	}
	return defer(lambdagen([this, self = slf.lock(), pattern = std::vector<char>(patBegin, patEnd)]([[maybe_unused]] const Yengine* engine, bool& done, std::optional<Future<IAIOResource::ReadResult>>& awao) -> std::variant<AFuture, movonly<IAIOResource::ReadResult>> {
		if(done) return IAIOResource::ReadResult::Err("Result already submitted!");
		if(awao){
			auto gmd = *awao;
			if(gmd->state() == FutureState::Completed){
				auto res = gmd->result();
				if(auto err = res->err()){
					done = true;
					return IAIOResource::ReadResult::Err(*err);
				}
				auto rd = *res->ok();
				MoveAppend(rd, readbuff);
			} else return gmd;
		}
		for(size_t i = 0; i < readbuff.size(); i++){
			auto pat = pattern.begin();
			size_t j = i;
			for(; j < readbuff.size() && pat != pattern.end(); j++, pat++) if(readbuff[j] != *pat) break;
			if(pat == pattern.end()){
				done = true;
				return *read<std::vector<char>>(j)->result();
			}
		}
		auto gmd = _read(1);
		awao.emplace(gmd);
		return gmd;
	}, std::optional<Future<IAIOResource::ReadResult>>(std::nullopt)));
}

template<> Future<IAIOResource::WriteResult> IAIOResource::write<std::vector<char>>(const std::vector<char>& dataRange);
template<> Future<IAIOResource::WriteResult> IAIOResource::write<std::vector<char>>(std::vector<char>&& dataRange);

using IORWriter = std::shared_ptr<IAIOResource::Writer>;
template<typename Data> auto operator<<(IORWriter wr, Data d){
	*wr << d;
	return wr;
}

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
		 * Opens asynchronous IO on the handled resource.
		 * @param r @consumes
		 * @returns async resource
		 */
		IOResource taek(HandledResource r);
	private:
		friend class IResource;
		void iothreadwork();
		#ifdef _WIN32
		static constexpr unsigned ioThreads = 1; //IO events are dispatched by notification to the engine
		#else
		fd_t cfdStopSend, cfdStopReceive;
		#endif
};

using FileOpenResult = result<IOResource, std::string>;
FileOpenResult fileOpenRead(Yengine*, const std::string& path);
FileOpenResult fileOpenWrite(Yengine*, const std::string& path);

}
