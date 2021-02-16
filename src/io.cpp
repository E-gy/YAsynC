#include "io.hpp"
#include <stdexcept>
#include <array>
#include "impls.hpp"

#include <iostream> //print error debug

#ifdef _WIN32
#include <fileapi.h>
#else
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h> //for perror reporting
#endif

constexpr size_t DEFAULT_BUFFER_SIZE = 4096;
#ifdef _WIN32
constexpr unsigned COMPLETION_KEY_SHUTDOWN = 1;
constexpr unsigned COMPLETION_KEY_IO = 2;
#else
#endif

namespace yasync::io {

IOYengine::IOYengine(Yengine* e) : engine(e) {
	#ifdef _WIN32
	ioCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, ioThreads);
	for(unsigned i = 0; i < ioThreads; i++){
		std::thread th([this](){ this->iothreadwork(); });
		th.detach();
	}
	#else
	ioEpoll = ::epoll_create1(EPOLL_CLOEXEC);
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

Yengine* IOYengine::yengine() const { return engine; }

#ifdef _WIN32
void PrintLastError(DWORD lerr){
	LPSTR err;
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_IGNORE_INSERTS, NULL, lerr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&err, 0, NULL);
	std::cerr << "Uh oh - " << err;
	LocalFree(err);
	throw std::runtime_error("uh oh :("); //FIXME no! Use. Results.
}
#else
static void PrintLastError(const std::string& errm){
	perror(errm.c_str());
	throw std::runtime_error(errm); //FIXME no! Use. Results.
}
#endif

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
	struct Trixter {
		OVERLAPPED overlapped;
		FileResource* cmon;
	};
	Trixter trixter = {};
	#else
	bool reged = false;
	bool lazyEpollReg(bool wr){
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
			} else PrintLastError("Register to epoll failed");
		}
		return reged = true;
	}
	void epollRearm(bool wr){
		if(!reged) return;
		::epoll_event epm;
		epm.events = (wr ? EPOLLOUT : EPOLLIN) | EPOLLONESHOT;
		epm.data.ptr = this;
		if(::epoll_ctl(engine->ioEpoll, EPOLL_CTL_MOD, file, &epm)) PrintLastError("Register to epoll failed");
	}
	#endif
	public:
		friend class IOYengine;
		FileResource(IOYengine* e, ResourceHandle rh) : engine(e), file(rh), buffer(), engif(new OutsideFuture<IOCompletionInfo>()) {
			#ifdef _WIN32
			trixter.cmon = this;
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
		Future<std::vector<char>> read(unsigned bytes){
			engif->s = FutureState::Running;
			//self.get() == this   exists to memory-lock dangling IO resource to this lambda generator
			return defer(lambdagen([this, self = slf.lock(), bytes]([[maybe_unused]] const Yengine* engine, bool& done, std::vector<char>& data) -> std::variant<AFuture, something<std::vector<char>>> {
				if(done) return data;
				#ifdef _WIN32
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
				#else
				if(lazyEpollReg(false)) return engif;
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
				epollRearm(false);
				#endif
				engif->s = FutureState::Running;
				return engif;
			}, std::vector<char>()));
		}
		Future<void> write(const std::vector<char>& data){
			engif->s = FutureState::Running;
			return defer(lambdagen([this, self = slf.lock()]([[maybe_unused]] const Yengine* engine, bool& done, std::vector<char>& data) -> std::variant<AFuture, something<void>> {
				if(data.empty()) done = true;
				if(done) return something<void>();
				#ifdef _WIN32
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
				DWORD transferred = 0;
				while(WriteFile(file, data.data(), data.size(), &transferred, &trixter.overlapped)){
					data.erase(data.begin(), data.begin()+transferred);
					trixter.overlapped.Offset += transferred;
				}
				if(::GetLastError() != ERROR_IO_PENDING) PrintLastError(::GetLastError());
				#else
				if(lazyEpollReg(true)) return engif;
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
				epollRearm(true);
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
		if(key == COMPLETION_KEY_IO) reinterpret_cast<FileResource::Trixter*>(overl)->cmon->notify(inf);
		#else
		::epoll_event event;
		auto es = ::epoll_wait(ioEpoll, &event, 1, -1);
		if(es < 0) PrintLastError("Epoll wait failed");
		else if(es == 0 || event.data.ptr == this) break;
		else reinterpret_cast<FileResource*>(event.data.ptr)->notify(event.events);
		#endif
	}
}

Resource IOYengine::taek(ResourceHandle rh){
	std::shared_ptr<FileResource> r(new FileResource(this, rh));
	r->setSelf(r);
	return r;
}

Resource IOYengine::fileOpenRead(const std::string& path){
	ResourceHandle file;
	#ifdef _WIN32
	file = CreateFileA(path.c_str(), GENERIC_READ, 0, NULL, OPEN_ALWAYS, FILE_FLAG_OVERLAPPED/* | FILE_FLAG_NO_BUFFERING cf https://docs.microsoft.com/en-us/windows/win32/fileio/file-buffering?redirectedfrom=MSDN */, NULL);
	if(file == INVALID_HANDLE_VALUE) PrintLastError(GetLastError());
	#else
	file = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if(file < 0) PrintLastError("Open file failed"); //FIXME no?
	#endif
	return taek(file);
}
Resource IOYengine::fileOpenWrite(const std::string& path){
	ResourceHandle file;
	#ifdef _WIN32
	file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_FLAG_OVERLAPPED, NULL);
	if(file == INVALID_HANDLE_VALUE) PrintLastError(GetLastError());
	#else
	file = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NONBLOCK | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if(file < 0) PrintLastError("Open file failed"); //FIXME no?
	#endif
	return taek(file);
}

}
