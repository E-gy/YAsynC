#include "io.hpp"
#include <stdexcept>
#include <array>
#include "impls.hpp"

#include <sstream>

#ifdef _WIN32
#include <fileapi.h>
#else
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#endif

constexpr size_t DEFAULT_BUFFER_SIZE = 4096;
#ifdef _WIN32
constexpr unsigned COMPLETION_KEY_SHUTDOWN = 1;
constexpr unsigned COMPLETION_KEY_IO = 2;
#else
#endif

namespace yasync::io {

IOYengine::IOYengine(Yengine* e) : engine(e),
	#ifdef _WIN32
	ioCompletionPort(CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, ioThreads))
	#else
	ioEpoll(::epoll_create1(EPOLL_CLOEXEC))
	#endif
{
	#ifdef _WIN32
	for(unsigned i = 0; i < ioThreads; i++){
		std::thread th([this](){ this->iothreadwork(); });
		th.detach();
	}
	#else
	if(ioEpoll < 0) throw std::runtime_error("Initalizing EPoll failed");
	fd_t pipe2[2];
	if(::pipe2(pipe2, O_CLOEXEC | O_NONBLOCK)) throw std::runtime_error("Initalizing close down pipe failed");
	cfdStopSend = pipe2[0];
	cfdStopReceive = pipe2[1];
	::epoll_event epm;
	epm.events = EPOLLHUP | EPOLLERR | EPOLLONESHOT;
	epm.data.ptr = this;
	if(::epoll_ctl(ioEpoll, EPOLL_CTL_ADD, cfdStopReceive, &epm)) throw std::runtime_error("Initalizing close down pipe epoll failed");
	std::thread th([this](){ this->iothreadwork(); });
	th.detach();
	#endif
}

IOYengine::~IOYengine(){
	#ifdef _WIN32
	for(unsigned i = 0; i < ioThreads; i++) PostQueuedCompletionStatus(ioCompletionPort, 0, COMPLETION_KEY_SHUTDOWN, NULL);
	CloseHandle(ioCompletionPort);
	#else
	close(cfdStopSend); //sends EPOLLHUP to receiving end
	close(cfdStopReceive);
	//hmmm...
	close(ioEpoll);
	#endif
}

/*result<void, int> IOYengine::iocplReg(ResourceHandle r, bool rearm){
	#ifdef _WIN32
	return !rearm && CreateIoCompletionPort(r, ioCompletionPort, COMPLETION_KEY_IO, 0) ? result<void, int>(GetLastError()) : result<void, int>();
	#else
	::epoll_event epm;
	epm.events = EPOLLOUT | EPOLLIN EPOLLONESHOT;
	epm.data.ptr = this;
	#endif
}*/

std::string printSysError(const std::string& message, syserr_t e){
	std::ostringstream compose;
	compose << message << ": ";
	#ifdef _WIN32
	LPSTR err;
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&err, 0, NULL);
	compose << err;
	LocalFree(err);
	#else
	compose << std::strerror(e); //FIXME not thread safe!
	// if(e < ::sys_nerr) compose << ::sys_errlist[e];
	// else compose << "Unknown error " << e;
	#endif
	return compose.str();
}
std::string printSysError(const std::string& message){
	return printSysError(message,
	#ifdef _WIN32
	::GetLastError()
	#else
	errno
	#endif
	);
}
template<typename S> result<S, std::string> retSysError(const std::string& message, syserr_t e){ return RError<S, std::string>(printSysError(message, e)); }
template<typename S> result<S, std::string> retSysError(const std::string& message){ return RError<S, std::string>(printSysError(message)); }

class FileResource : public IAIOResource {
	std::weak_ptr<FileResource> slf;
	IOYengine* engine;
	ResourceHandle file;
	std::array<char, DEFAULT_BUFFER_SIZE> buffer;
	std::shared_ptr<OutsideFuture<IOCompletionInfo>> engif;
	void notify(IOCompletionInfo inf){
		engif->r.emplace(inf);
		engif->s = FutureState::Completed;
		engine->engine->notify(std::dynamic_pointer_cast<IFutureT<IOCompletionInfo>>(engif));
	}
	#ifdef _WIN32
	#else
	bool reged = false;
	result<bool, std::string> lazyEpollReg(bool wr){
		if(reged) return false;
		::epoll_event epm;
		epm.events = (wr ? EPOLLOUT : EPOLLIN) | EPOLLONESHOT;
		epm.data.ptr = this;
		if(::epoll_ctl(engine->ioEpoll, EPOLL_CTL_ADD, file, &epm)){
			if(errno == EPERM){
				//The file does not support non-blocking io :(
				//That means that all r/w will succeed (and block). So we report ourselves ready for IO, and off to EOD we go!
				engif->r.emplace(wr ? EPOLLOUT : EPOLLIN);
				engif->s = FutureState::Completed;
				return !(reged = true);
			} else return retSysError<bool>("Register to epoll failed");
		}
		return reged = true;
	}
	result<void, std::string> epollRearm(bool wr){
		if(!reged) return ROk<std::string>();
		::epoll_event epm;
		epm.events = (wr ? EPOLLOUT : EPOLLIN) | EPOLLONESHOT;
		epm.data.ptr = this;
		return ::epoll_ctl(engine->ioEpoll, EPOLL_CTL_MOD, file, &epm) ? retSysError<void>("Register to epoll failed") : ROk<std::string>();
	}
	#endif
	public:
		friend class IOYengine;
		FileResource(IOYengine* e, ResourceHandle rh) : engine(e), file(rh), buffer(), engif(new OutsideFuture<IOCompletionInfo>()) {
			#ifdef _WIN32
			CreateIoCompletionPort(file, e->ioCompletionPort, COMPLETION_KEY_IO, 0);
			#else
			#endif
		}
		FileResource(const FileResource& cpy) = delete;
		FileResource(FileResource&& mov) = delete;
		~FileResource(){
			#ifdef _WIN32
			if(file != INVALID_HANDLE_VALUE) CloseHandle(file);
			#else
			if(file >= 0) close(file);
			#endif
		}
		auto setSelf(std::shared_ptr<FileResource> self){
			return slf = self;
		}
		Future<result<std::vector<char>, std::string>> read(unsigned bytes){
			engif->s = FutureState::Running;
			//self.get() == this   exists to memory-lock dangling IO resource to this lambda generator
			return defer(lambdagen([this, self = slf.lock(), bytes]([[maybe_unused]] const Yengine* engine, bool& done, std::vector<char>& data) -> std::variant<AFuture, something<result<std::vector<char>, std::string>>> {
				if(done) return ROk<std::vector<char>, std::string>(data);
				#ifdef _WIN32
				if(engif->s == FutureState::Completed){
					IOCompletionInfo result = *engif->r;
					if(!result.status){
						if(result.lerr == ERROR_HANDLE_EOF){
							done = true;
							return ROk<std::vector<char>, std::string>(data);
						} else return retSysError<std::vector<char>>("Async Read failure", result.lerr);
					} else {
						data.insert(data.end(), buffer.begin(), buffer.begin()+result.transferred);
						overlapped()->Offset += result.transferred;
						if(bytes > 0 && (done = data.size() >= bytes)){
							done = true;
							return ROk<std::vector<char>, std::string>(data);;
						}
					}
				}
				DWORD transferred = 0;
				while(ReadFile(file, buffer.begin(), bytes == 0 ? buffer.size() : std::min(buffer.size(), bytes - data.size()), &transferred, overlapped())){
					data.insert(data.end(), buffer.begin(), buffer.begin() + transferred);
					overlapped()->Offset += transferred;
					if(bytes > 0 && data.size() >= bytes){
						done = true;
						return ROk<std::vector<char>, std::string>(data);;
					}
				}
				switch(::GetLastError()){
					case ERROR_IO_PENDING: break;
					case ERROR_HANDLE_EOF:
						done = true;
						return ROk<std::vector<char>, std::string>(data);;
					default: return retSysError<std::vector<char>>("Sync Read failure");
				}
				#else
				{
					auto rr = lazyEpollReg(false);
					if(auto err = rr.error()) return RError<std::vector<char>>(*err);
					else if(*rr.ok()) return engif;
				}
				if(engif->s == FutureState::Completed){
					int leve = *engif->r;
					if(leve != EPOLLIN) return retSysError<std::vector<char>>("Epoll wrong event");
					int transferred;
					while((transferred = ::read(file, buffer.data(), bytes == 0 ? buffer.size() : std::min(buffer.size(), bytes - data.size()))) > 0){
						data.insert(data.end(), buffer.begin(), buffer.begin()+transferred);
						if(bytes > 0 && data.size() >= bytes){
							done = true;
							return ROk<std::vector<char>, std::string>(data);;
						}
					}
					if(transferred == 0){
						done = true;
						return ROk<std::vector<char>, std::string>(data);;
					}
					if(errno != EWOULDBLOCK && errno != EAGAIN) return retSysError<std::vector<char>>("Read failed");
				}
				if(auto e = epollRearm(true).error()) return RError<std::vector<char>>(*e);
				#endif
				engif->s = FutureState::Running;
				return engif;
			}, std::vector<char>()));
		}
		Future<result<void, std::string>> write(const std::vector<char>& data){
			engif->s = FutureState::Running;
			return defer(lambdagen([this, self = slf.lock()]([[maybe_unused]] const Yengine* engine, bool& done, std::vector<char>& data) -> std::variant<AFuture, something<result<void, std::string>>> {
				if(data.empty()) done = true;
				if(done) return ROk<std::string>();
				#ifdef _WIN32
				if(engif->s == FutureState::Completed){
					IOCompletionInfo result = *engif->r;
					if(!result.status) return retSysError<void>("Async Write failed", result.lerr);
					else {
						data.erase(data.begin(), data.begin()+result.transferred);
						overlapped()->Offset += result.transferred;
						if(data.empty()){
							done = true;
							return ROk<std::string>();
						}
					}
				}
				DWORD transferred = 0;
				while(WriteFile(file, data.data(), data.size(), &transferred, overlapped())){
					data.erase(data.begin(), data.begin()+transferred);
					overlapped()->Offset += transferred;
				}
				if(::GetLastError() != ERROR_IO_PENDING) return retSysError<void>("Sync Write failed");
				#else
				{
					auto rr = lazyEpollReg(false);
					if(auto err = rr.error()) return RError<void>(*err);
					else if(*rr.ok()) return engif;
				}
				if(engif->s == FutureState::Completed){
					int leve = *engif->r;
					if(leve != EPOLLOUT) return retSysError<void>("Epoll wrong event");
					int transferred;
					while((transferred = ::write(file, data.data(), data.size())) >= 0){
						data.erase(data.begin(), data.begin()+transferred);
						if(data.empty()){
							done = true;
							return ROk<std::string>();
						}
					}
					if(errno != EWOULDBLOCK && errno != EAGAIN) return retSysError<void>("Write failed");
				}
				if(auto e = epollRearm(true).error()) return RError<void>(*e);
				#endif
				engif->s = FutureState::Running;
				return engif;
			}, std::move(data)));
		}
};

void IOYengine::iothreadwork(){
	while(true){
		#ifdef _WIN32
		IOCompletionInfo inf;
		ULONG_PTR key;
		LPOVERLAPPED overl;
		inf.status = GetQueuedCompletionStatus(ioCompletionPort, &inf.transferred, &key, &overl, INFINITE);
		inf.lerr = GetLastError();
		if(key == COMPLETION_KEY_SHUTDOWN) break;
		if(key == COMPLETION_KEY_IO) reinterpret_cast<IResource::Overlapped*>(overl)->resource->notify(inf);
		#else
		::epoll_event event;
		auto es = ::epoll_wait(ioEpoll, &event, 1, -1);
		if(es < 0) throw std::runtime_error(printSysError("Epoll wait failed"));
		else if(es == 0 || event.data.ptr == this) break;
		else reinterpret_cast<IResource*>(event.data.ptr)->notify(event.events);
		#endif
	}
}

IOResource IOYengine::taek(ResourceHandle rh){
	std::shared_ptr<FileResource> r(new FileResource(this, rh));
	r->setSelf(r);
	return r;
}

result<IOResource, std::string> IOYengine::fileOpenRead(const std::string& path){
	ResourceHandle file;
	#ifdef _WIN32
	file = CreateFileA(path.c_str(), GENERIC_READ, 0, NULL, OPEN_ALWAYS, FILE_FLAG_OVERLAPPED/* | FILE_FLAG_NO_BUFFERING cf https://docs.microsoft.com/en-us/windows/win32/fileio/file-buffering?redirectedfrom=MSDN */, NULL);
	if(file == INVALID_HANDLE_VALUE) retSysError<IOResource>("Open File failed", GetLastError());
	#else
	file = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if(file < 0) retSysError<IOResource>("Open file failed", errno);
	#endif
	return taek(file);
}
result<IOResource, std::string> IOYengine::fileOpenWrite(const std::string& path){
	ResourceHandle file;
	#ifdef _WIN32
	file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_FLAG_OVERLAPPED, NULL);
	if(file == INVALID_HANDLE_VALUE) retSysError<IOResource>("Open File failed", GetLastError());
	#else
	file = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NONBLOCK | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if(file < 0) retSysError<IOResource>("Open file failed", errno);
	#endif
	return taek(file);
}

}
