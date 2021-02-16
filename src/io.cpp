#include "io.hpp"
#include <stdexcept>
#include <array>
#include "impls.hpp"

//Common
namespace yasync::io {

Yengine* IOYengine::yengine() const { return engine; }

}

#ifdef _WIN32

#include <fileapi.h>
#include <iostream> //print error debug

namespace yasync::io {

constexpr size_t DEFAULT_BUFFER_SIZE = 4096;
constexpr unsigned COMPLETION_KEY_SHUTDOWN = 1;
constexpr unsigned COMPLETION_KEY_IO = 2;

IOYengine::IOYengine(Yengine* e) : engine(e) {
	ioCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, ioThreads);
	for(unsigned i = 0; i < ioThreads; i++){
		std::thread th([this](){ this->iothreadwork(); });
		th.detach();
	}
}

IOYengine::~IOYengine(){
	for(unsigned i = 0; i < ioThreads; i++) PostQueuedCompletionStatus(ioCompletionPort, 0, COMPLETION_KEY_SHUTDOWN, NULL);
	CloseHandle(ioCompletionPort);
}

void PrintLastError(DWORD lerr){
	LPSTR err;
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_IGNORE_INSERTS, NULL, lerr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&err, 0, NULL);
	std::cerr << "Uh oh - " << err;
	LocalFree(err);
	throw std::runtime_error("uh oh :("); //FIXME no! Use. Results.
}

struct IOCompletionInfo {
	BOOL status;
	DWORD transferred;
	DWORD lerr;
};

class FileResource : public IAIOResource {
	enum class Mode {
		Read, Write
	};
	std::weak_ptr<FileResource> slf;
	struct Trixter {
		OVERLAPPED overlapped;
		FileResource* cmon;
	};
	Trixter trixter = {};
	IOYengine* engine;
	Mode mode;
	HANDLE file;
	std::array<char, DEFAULT_BUFFER_SIZE> buffer;
	std::shared_ptr<OutsideFuture<IOCompletionInfo>> engif;
	public:
		friend class IOYengine;
		FileResource(IOYengine* e, Mode m, const std::string& path) : engine(e), mode(m), buffer(), engif(new OutsideFuture<IOCompletionInfo>()) {
			trixter.cmon = this;
			file = CreateFileA(path.c_str(), mode == Mode::Write ? GENERIC_WRITE : GENERIC_READ, 0, NULL, OPEN_ALWAYS, FILE_FLAG_OVERLAPPED/* | FILE_FLAG_NO_BUFFERING cf https://docs.microsoft.com/en-us/windows/win32/fileio/file-buffering?redirectedfrom=MSDN */, NULL);
			if(file == INVALID_HANDLE_VALUE) throw std::runtime_error("CreateFile failed :("); //FIXME no?
			CreateIoCompletionPort(file, e->ioCompletionPort, COMPLETION_KEY_IO, 0);
		}
		FileResource(const FileResource& cpy) = delete;
		FileResource(FileResource&& mov) = delete;
		~FileResource(){
			if(file != INVALID_HANDLE_VALUE) CloseHandle(file);
		}
		auto setSelf(std::shared_ptr<FileResource> self){
			return slf = self;
		}
		Future<std::vector<char>> read(unsigned bytes){
			//self.get() == this   exists to memory-lock dangling IO resource to this lambda generator
			return defer(lambdagen([this, self = slf.lock(), bytes]([[maybe_unused]] const Yengine* engine, bool& done, std::vector<char>& data) -> std::variant<AFuture, something<std::vector<char>>> {
				if(done) return data;
				if(engif->s == FutureState::Completed){
					IOCompletionInfo result = *engif->r;
					if(!result.status){
						if(result.lerr == ERROR_HANDLE_EOF){
							done = true;
							return data;
						} else PrintLastError(result.lerr);
					} else {
						data.insert(data.end(), buffer.begin(), buffer.begin()+result.transferred);
						trixter.overlapped.Offset += result.transferred;
						if(bytes > 0 && (done = data.size() >= bytes)){
							done = true;
							return data;
						}
					}
				}
				engif->s = FutureState::Running;
				DWORD transferred = 0;
				while(ReadFile(file, buffer.begin(), bytes == 0 ? buffer.size() : std::min(buffer.size(), bytes - data.size()), &transferred, &trixter.overlapped)){
					data.insert(data.end(), buffer.begin(), buffer.begin() + transferred);
					trixter.overlapped.Offset += transferred;
					if(bytes > 0 && data.size() >= bytes){
						done = true;
						return data;
					}
				}
				switch(::GetLastError()){
					case ERROR_IO_PENDING: break;
					case ERROR_HANDLE_EOF:
						done = true;
						return data;
					default: PrintLastError(::GetLastError());
				}
				return engif;
			}, std::vector<char>()));
		}
		Future<void> write(const std::vector<char>& data){
			return defer(lambdagen([this, self = slf.lock()]([[maybe_unused]] const Yengine* engine, bool& done, std::vector<char>& data) -> std::variant<AFuture, something<void>> {
				if(data.empty()) done = true;
				if(done) return something<void>();
				if(engif->s == FutureState::Completed){
					IOCompletionInfo result = *engif->r;
					if(!result.status) PrintLastError(result.lerr);
					else {
						data.erase(data.begin(), data.begin()+result.transferred);
						trixter.overlapped.Offset += result.transferred;
						if(data.empty()){
							done = true;
							return something<void>();
						}
					}
				}
				engif->s = FutureState::Running;
				DWORD transferred = 0;
				while(WriteFile(file, data.data(), data.size(), &transferred, &trixter.overlapped)){
					data.erase(data.begin(), data.begin()+transferred);
					trixter.overlapped.Offset += transferred;
				}
				if(::GetLastError() != ERROR_IO_PENDING) PrintLastError(::GetLastError());
				return engif;
			}, std::move(data)));
		}
};

void IOYengine::iothreadwork(){
	while(true){
		IOCompletionInfo inf;
		ULONG_PTR key;
		LPOVERLAPPED overl;
		inf.status = GetQueuedCompletionStatus(ioCompletionPort, &inf.transferred, &key, &overl, INFINITE);
		if(key == COMPLETION_KEY_SHUTDOWN) break;
		if(key == COMPLETION_KEY_IO){
			inf.lerr = GetLastError();
			auto resource = reinterpret_cast<FileResource::Trixter*>(overl)->cmon;
			resource->engif->s = FutureState::Completed;
			resource->engif->r.emplace(inf);
			engine->notify(std::dynamic_pointer_cast<IFutureT<IOCompletionInfo>>(resource->engif));
		}
	}
}

Resource IOYengine::fileOpenRead(const std::string& path){
	std::shared_ptr<FileResource> r(new FileResource(this, FileResource::Mode::Read, path));
	r->setSelf(r);
	return r;
}
Resource IOYengine::fileOpenWrite(const std::string& path){
	std::shared_ptr<FileResource> r(new FileResource(this, FileResource::Mode::Write, path));
	r->setSelf(r);
	return r;
}

}

#else

#include <sys/epoll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h> //for perror reporting
#include <iostream>

namespace yasync::io {

constexpr size_t DEFAULT_BUFFER_SIZE = 4096;
constexpr unsigned EPOLL_MAX_EVENTS = 64;

IOYengine::IOYengine(Yengine* e) : engine(e) {
	ioEpoll = ::epoll_create1(EPOLL_CLOEXEC);
	if(ioEpoll < 0) throw std::runtime_error("Initalizing EPoll failed");
	fd_t pipe2[2];
	if(::pipe2(pipe2, O_CLOEXEC | O_NONBLOCK)) throw std::runtime_error("Initalizing close down pipe failed");
	::epoll_event epm;
	epm.events = EPOLLHUP | EPOLLERR;
	epm.data.ptr = this;
	if(::epoll_ctl(ioEpoll, EPOLL_CTL_ADD, cfdStopReceive, &epm)) throw std::runtime_error("Initalizing close down pipe epoll failed");
	std::thread th([this](){ this->iothreadwork(); });
	th.detach();
}

IOYengine::~IOYengine(){
	close(cfdStopSend); //sends EPOLLHUP to receiving end
	close(cfdStopReceive);
	//hmmm...
	close(ioEpoll);
}

static void PrintLastError(const std::string& errm){
	perror(errm.c_str());
	throw std::runtime_error(errm); //FIXME no! Use. Results.
}

class FileResource : public IAIOResource {
	enum class Mode {
		Read, Write
	};
	std::weak_ptr<FileResource> slf;
	IOYengine* engine;
	Mode mode;
	fd_t file;
	std::array<char, DEFAULT_BUFFER_SIZE> buffer;
	std::shared_ptr<OutsideFuture<int>> engif;
	bool reged = false;
	bool lazyEpollReg(){
		if(reged) return false;
		::epoll_event epm;
		epm.events = mode == Mode::Write ? EPOLLOUT : EPOLLIN;
		epm.data.ptr = this;
		if(::epoll_ctl(engine->ioEpoll, EPOLL_CTL_ADD, file, &epm)){
			if(errno == EPERM){
				//The file does not support non-blocking io :(
				//That means that all r/w will succeed (and block). So we report ourselves ready for IO, and off to EOD we go!
				engif->r.emplace(mode == Mode::Write ? EPOLLOUT : EPOLLIN);
				engif->s = FutureState::Completed;
				return !(reged = true);
			} else PrintLastError("Register to epoll failed");
		}
		return reged = true;
	}
	public:
		friend class IOYengine;
		FileResource(IOYengine* e, Mode m, const std::string& path) : engine(e), mode(m), buffer(), engif(new OutsideFuture<int>()) {
			file = open(path.c_str(), (mode == Mode::Write ? O_WRONLY | O_CREAT : O_RDONLY) | O_NONBLOCK | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
			if(file < 0) PrintLastError("Open file failed"); //FIXME no?
		}
		FileResource(const FileResource& cpy) = delete;
		FileResource(FileResource&& mov) = delete;
		~FileResource(){
			if(file >= 0) close(file);
		}
		auto setSelf(std::shared_ptr<FileResource> self){
			return slf = self;
		}
		Future<std::vector<char>> read(unsigned bytes){
			return defer(lambdagen([this, self = slf.lock(), bytes]([[maybe_unused]] const Yengine* engine, bool& done, std::vector<char>& data) -> std::variant<AFuture, something<std::vector<char>>> {
				if(done) return data;
				if(lazyEpollReg()) return engif;
				if(engif->s == FutureState::Completed){
					int leve = *engif->r;
					if(leve != EPOLLIN) PrintLastError("Epoll wrong event");
					int transferred;
					while((transferred = ::read(file, buffer.data(), bytes == 0 ? buffer.size() : std::min(buffer.size(), bytes - data.size()))) > 0){
						data.insert(data.end(), buffer.begin(), buffer.begin()+transferred);
						if(bytes > 0 && data.size() >= bytes){
							done = true;
							return data;
						}
					}
					if(transferred == 0){
						done = true;
						return data;
					}
					if(errno != EWOULDBLOCK && errno != EAGAIN) PrintLastError("Read failed");
				}
				engif->s = FutureState::Running;
				return engif;
			}, std::vector<char>()));
		}
		Future<void> write(const std::vector<char>& data){
			return defer(lambdagen([this, self = slf.lock()]([[maybe_unused]] const Yengine* engine, bool& done, std::vector<char>& data) -> std::variant<AFuture, something<void>> {
				if(data.empty()) done = true;
				if(done) return something<void>();
				if(lazyEpollReg()) return engif;
				if(engif->s == FutureState::Completed){
					int leve = *engif->r;
					if(leve != EPOLLOUT) PrintLastError("Epoll wrong event");
					int transferred;
					while((transferred = ::write(file, data.data(), data.size())) >= 0){
						data.erase(data.begin(), data.begin()+transferred);
						if(data.empty()){
							done = true;
							return something<void>();
						}
					}
					if(errno != EWOULDBLOCK && errno != EAGAIN) PrintLastError("Write failed");
				}
				engif->s = FutureState::Running;
				return engif;
			}, std::move(data)));
		}
};

void IOYengine::iothreadwork(){
	while(true){
		::epoll_event events[EPOLL_MAX_EVENTS];
		auto es = ::epoll_wait(ioEpoll, events, EPOLL_MAX_EVENTS, -1);
		for(int i = 0; i < es; i++)
			if(events[i].data.ptr == this) return;
			else {
				auto resource = reinterpret_cast<FileResource*>(events[i].data.ptr);
				resource->engif->s = FutureState::Completed;
				auto eveid = events[i].events;
				resource->engif->r.emplace(eveid);
				engine->notify(std::dynamic_pointer_cast<IFutureT<void>>(resource->engif));
			}
	}
}

Resource IOYengine::fileOpenRead(const std::string& path){
	std::shared_ptr<FileResource> r(new FileResource(this, FileResource::Mode::Read, path));
	r->setSelf(r);
	return r;
}
Resource IOYengine::fileOpenWrite(const std::string& path){
	std::shared_ptr<FileResource> r(new FileResource(this, FileResource::Mode::Write, path));
	r->setSelf(r);
	return r;
}

}

#endif
