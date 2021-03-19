#include "io.hpp"
#include <stdexcept>
#include <array>
#include "impls.hpp"

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

namespace yasync::io {

class StandardHandledResource : public IHandledResource {
	public:
		StandardHandledResource(ResourceHandle f) : IHandledResource(f) {}
		~StandardHandledResource(){
			#ifdef _WIN32
			if(rh != INVALID_HANDLE_VALUE) CloseHandle(rh);
			#else
			if(rh >= 0) close(rh);
			#endif
		}
};

IHandledResource::IHandledResource(ResourceHandle r, bool b) : rh(r), iopor(b) {}
IHandledResource::~IHandledResource(){}

class FileResource : public IAIOResource {
	IOYengine::Ticket ioengine;
	HandledResource res;
	std::array<char, DEFAULT_BUFFER_SIZE> buffer;
	std::shared_ptr<OutsideFuture<IOCompletionInfo>> engif;
	void notify(IOCompletionInfo inf){
		engif->r = inf;
		engif->s = FutureState::Completed;
		engine->notify(engif);
	}
	#ifdef _WIN32
	#else
	using EPollRegResult = result<bool, std::string>;
	EPollRegResult lazyEpollReg(bool wr){
		if(res->iopor) return false;
		::epoll_event epm;
		epm.events = (wr ? EPOLLOUT : EPOLLIN) | EPOLLONESHOT;
		epm.data.ptr = this;
		if(::epoll_ctl(ioengine->ioPo->rh, EPOLL_CTL_ADD, res->rh, &epm)){
			if(errno == EPERM){
				//The file does not support non-blocking io :(
				//That means that all r/w will succeed (and block). So we report ourselves ready for IO, and off to EOD we go!
				engif->r = wr ? EPOLLOUT : EPOLLIN;
				engif->s = FutureState::Completed;
				return !(res->iopor = true);
			} else return retSysError<EPollRegResult>("Register to epoll failed");
		}
		return res->iopor = true;
	}
	using EPollRearmResult = result<void, std::string>;
	EPollRearmResult epollRearm(bool wr){
		if(!res->iopor) return EPollRearmResult::Ok();
		::epoll_event epm;
		epm.events = (wr ? EPOLLOUT : EPOLLIN) | EPOLLONESHOT;
		epm.data.ptr = this;
		return ::epoll_ctl(ioengine->ioPo->rh, EPOLL_CTL_MOD, res->rh, &epm) ? retSysError<EPollRearmResult>("Register to epoll failed") : EPollRearmResult::Ok();
	}
	#endif
	public:
		friend class IOYengine;
		FileResource(IOYengine* e, HandledResource hr) : IAIOResource(e->engine), ioengine(e->ticket()), res(std::move(hr)), buffer(), engif(new OutsideFuture<IOCompletionInfo>()) {
			#ifdef _WIN32
			if(!res->iopor){
				::CreateIoCompletionPort(res->rh, e->ioPo->rh, COMPLETION_KEY_IO, 0);
				res->iopor = true;
				::SetFileCompletionNotificationModes(res->rh, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
			}
			#else
			#endif
		}
		FileResource(const FileResource& cpy) = delete;
		FileResource(FileResource&& mov) = delete;
		~FileResource(){}
		Future<ReadResult> _read(size_t bytes){
			//self.get() == this   exists to memory-lock dangling IO resource to this lambda generator
			return defer(lambdagen([this, self = slf.lock(), bytes](const Yengine*, bool& done, std::vector<char>& data) -> std::variant<AFuture, movonly<ReadResult>> {
				if(done) return ReadResult::Ok(data);
				#ifdef _WIN32 //TODO FIXME a UB lives somewhere in here, making itself known only on large data reads
				if(engif->s == FutureState::Completed){
					engif->s = FutureState::Running;
					IOCompletionInfo result = *engif->r;
					if(!result.status) switch(result.lerr){
						case ERROR_HANDLE_EOF:
							done = true;
							return ReadResult::Ok(data);
						case ERROR_OPERATION_ABORTED:
							done = true;
							return ReadResult::Err("Operation cancelled (hang up on the other side, or cancellation requested)");
						default:
							done = true;
							return retSysError<ReadResult>("Async Read failure", result.lerr);
					} else {
						if(result.transferred == 0){ //EOF on ECONNRESET
							done = true;
							return ReadResult::Ok(data);
						}
						data.insert(data.end(), buffer.begin(), buffer.begin()+result.transferred);
						overlapped()->Offset += result.transferred;
						if(bytes > 0 && (done = data.size() >= bytes)){
							done = true;
							return ReadResult::Ok(data);;
						}
					}
				}
				DWORD transferred = 0;
				while(ReadFile(res->rh, buffer.begin(), buffer.size(), &transferred, overlapped())){
					data.insert(data.end(), buffer.begin(), buffer.begin() + transferred);
					overlapped()->Offset += transferred;
					if(bytes > 0 && data.size() >= bytes){
						done = true;
						return ReadResult::Ok(data);;
					}
				}
				switch(::GetLastError()){
					case ERROR_IO_PENDING: break;
					case ERROR_HANDLE_EOF:
						done = true;
						return ReadResult::Ok(data);;
					default:
						done = true;
						return retSysError<ReadResult>("Sync Read failure");
				}
				#else
				{
					auto rr = lazyEpollReg(false);
					if(auto err = rr.err()) return ReadResult::Err(*err);
					else if(*rr.ok()) return engif;
				}
				if(engif->s == FutureState::Completed){
					engif->s = FutureState::Running;
					int leve = *engif->r;
					switch(leve){
						case EPOLLIN: {
							int transferred;
							while((transferred = ::read(res->rh, buffer.data(), buffer.size())) > 0){
								data.insert(data.end(), buffer.begin(), buffer.begin()+transferred);
								if(bytes > 0 && data.size() >= bytes){
									done = true;
									return ReadResult::Ok(data);;
								}
							}
							if(transferred == 0){
								done = true;
								return ReadResult::Ok(data);;
							}
							if(errno != EWOULDBLOCK && errno != EAGAIN){
								done = true;
								return retSysError<ReadResult>("Read failed");
							}
							break;
						}
						case EPOLLHUP:
							done = true;
							return ReadResult::Err("Operation cancelled (hang up on the other side, or cancellation requested)");
						case EPOLLPRI:
						case EPOLLOUT: //NO! BAD EPOLL!
						default: {
							done = true;
							std::ostringstream erm;
							erm << "Epoll wrong event (" << leve << ")\n";
							return retSysError<ReadResult>(erm.str());
						}
					}
				}
				if(auto e = epollRearm(false).err()){
					std::cout << "epoll rearm failed for " << engif.get() << "\n";
					done = true;
					return ReadResult::Err(*e);
				}
				#endif
				return engif;
			}, std::vector<char>()));
		}
		Future<WriteResult> _write(std::vector<char>&& data){
			return defer(lambdagen([this, self = slf.lock()](const Yengine*, bool& done, std::vector<char>& data) -> std::variant<AFuture, movonly<WriteResult>> {
				if(data.empty()) done = true;
				if(done) return WriteResult::Ok();
				#ifdef _WIN32
				if(engif->s == FutureState::Completed){
					engif->s = FutureState::Running;
					IOCompletionInfo result = *engif->r;
					if(!result.status) switch(result.lerr){
						case ERROR_OPERATION_ABORTED:
							done = true;
							return WriteResult::Err("Operation cancelled (hang up on the other side, or cancellation requested)");
						default:
							done = true;
							return retSysError<WriteResult>("Async Write failed", result.lerr);
					} else {
						data.erase(data.begin(), data.begin()+result.transferred);
						overlapped()->Offset += result.transferred;
						if(data.empty()){
							done = true;
							return WriteResult::Ok();
						}
					}
				}
				DWORD transferred = 0;
				while(WriteFile(res->rh, data.data(), data.size(), &transferred, overlapped())){
					data.erase(data.begin(), data.begin()+transferred);
					overlapped()->Offset += transferred;
					if(data.empty()){
						done = true;
						return WriteResult::Ok();
					}
				}
				if(::GetLastError() != ERROR_IO_PENDING){
					done = true;
					return retSysError<WriteResult>("Sync Write failed");
				}
				#else
				{
					auto rr = lazyEpollReg(true);
					if(auto err = rr.err()) return WriteResult::Err(*err);
					else if(*rr.ok()) return engif;
				}
				if(engif->s == FutureState::Completed){
					engif->s = FutureState::Running;
					int leve = *engif->r;
					switch(leve){
						case EPOLLOUT: {
							int transferred;
							while((transferred = ::write(res->rh, data.data(), data.size())) >= 0){
								data.erase(data.begin(), data.begin()+transferred);
								if(data.empty()){
									done = true;
									return WriteResult::Ok();
								}
							}
							if(errno != EWOULDBLOCK && errno != EAGAIN){
								done = true;
								return retSysError<WriteResult>("Write failed");
							}
							break;
						}
						case EPOLLHUP:
							done = true;
							return WriteResult::Err("Operation cancelled (hang up on the other side, or cancellation requested)");
						case EPOLLPRI:
						case EPOLLIN: //NO! BAD EPOLL!
						default: {
							done = true;
							std::ostringstream erm;
							erm << "Epoll wrong event (" << leve << ")\n";
							return retSysError<WriteResult>(erm.str());
						}
					}
				}
				if(auto e = epollRearm(true).err()){
					std::cout << "epoll rearm failed for " << engif.get() << "\n";
					done = true;
					return WriteResult::Err(*e);
				}
				#endif
				return engif;
			}, data));
		}
};

template<> Future<IAIOResource::ReadResult> IAIOResource::read<std::vector<char>>(){
	if(readbuff.empty()) return _read(0);
	return _read(0) >> [this, self = slf.lock()](auto rr){ return rr.mapOk([this](auto data){
		std::vector<char> nd;
		nd.reserve(readbuff.size() + data.size());
		MoveAppend(readbuff, nd);
		MoveAppend(data, nd);
		return nd;
	});};
}
template<> Future<IAIOResource::ReadResult> IAIOResource::read<std::vector<char>>(size_t upto){
	if(readbuff.size() >= upto){
		std::vector<char> ret(readbuff.begin(), readbuff.begin()+upto);
		readbuff.erase(readbuff.begin(), readbuff.begin()+upto);
		return completed(IAIOResource::ReadResult(std::move(ret)));
	}
	auto n2r = upto-readbuff.size();
	return _read(n2r) >> [this, self = slf.lock(), n2r](auto rr){ return rr.mapOk([=](auto data){
		std::vector<char> nd;
		nd.reserve(readbuff.size() + std::min(n2r, data.size()));
		MoveAppend(readbuff, nd);
		if(data.size() > n2r){
			auto sfl = data.begin()+n2r;
			std::move(sfl, data.end(), std::back_inserter(readbuff));
			std::move(data.begin(), sfl, std::back_inserter(nd));
			data.clear();
		} else MoveAppend(data, nd);
		return nd;
	});};
}

template<> Future<IAIOResource::WriteResult> IAIOResource::write<std::vector<char>>(const std::vector<char>& dataRange){
	return _write(std::vector<char>(dataRange));
}
template<> Future<IAIOResource::WriteResult> IAIOResource::write<std::vector<char>>(std::vector<char>&& dataRange){
	return _write(std::move(dataRange));
}

//L2

IAIOResource::Writer::Writer(IOResource r) : resource(r), eodnot(new OutsideFuture<IAIOResource::WriteResult>()), lflush(completed(IAIOResource::WriteResult())) {}
IAIOResource::Writer::~Writer(){
	resource->engine <<= flush() >> [res = resource, naut = eodnot](auto wr){
		naut->r = wr;
		naut->s = FutureState::Completed;
		res->engine->notify(naut);
	};
}
Future<IAIOResource::WriteResult> IAIOResource::Writer::eod() const { return eodnot; }
Future<IAIOResource::WriteResult> IAIOResource::Writer::flush(){
	std::vector<char> buff;
	std::swap(buff, buffer);
	return lflush = (lflush >> [res = resource, buff](auto lr){
		if(lr.err()) return completed(lr);
		else return res->write(buff);
	});
}

IORWriter IAIOResource::writer(){
	return IORWriter(new Writer(slf.lock()));
}


// IO Yengine

IOYengine::IOYengine(Yengine* e) : engine(e),
	#ifdef _WIN32
	ioPo(new StandardHandledResource(CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, ioThreads)))
	#else
	ioPo(new StandardHandledResource(::epoll_create1(EPOLL_CLOEXEC)))
	#endif
{
	#ifdef _WIN32
	for(unsigned i = 0; i < ioThreads; i++) Daemons::launch([this](){ iothreadwork(); });
	#else
	if(ioPo->rh < 0) throw std::runtime_error("Initalizing EPoll failed");
	fd_t pipe2[2];
	if(::pipe2(pipe2, O_CLOEXEC | O_NONBLOCK)) throw std::runtime_error("Initalizing close down pipe failed");
	cfdStopSend = SharedResource(new StandardHandledResource(pipe2[1]));
	cfdStopReceive = SharedResource(new StandardHandledResource(pipe2[0]));
	::epoll_event epm;
	epm.events = EPOLLHUP | EPOLLERR | EPOLLONESHOT;
	epm.data.ptr = this;
	if(::epoll_ctl(ioPo->rh, EPOLL_CTL_ADD, cfdStopReceive->rh, &epm)) throw std::runtime_error("Initalizing close down pipe epoll failed");
	Daemons::launch([this](){ iothreadwork(); });
	#endif
}

void IOYengine::wioe(){
	{
		std::unique_lock lok(ticketsLock);
		while(tickets > 0) condWIOE.wait(lok);
	}
	#ifdef _WIN32
	for(unsigned i = 0; i < ioThreads; i++) PostQueuedCompletionStatus(ioPo->rh, 0, COMPLETION_KEY_SHUTDOWN, NULL);
	#else
	cfdStopSend.reset();
	#endif
}


IOYengine::Ticket::Ticket() : engine(nullptr) {} 
IOYengine::Ticket::Ticket(IOYengine* e) : engine(e){
	std::unique_lock lok(e->ticketsLock);
	e->tickets++;
}
IOYengine::Ticket::~Ticket(){
	release();
}
void IOYengine::Ticket::release(){
	if(engine){
		std::unique_lock lok(engine->ticketsLock);
		if(--engine->tickets == 0) engine->condWIOE.notify_all();
		engine = nullptr;
	}
}
IOYengine::Ticket::Ticket(Ticket && mov){
	engine = mov.engine;
	mov.engine = nullptr;
}
IOYengine::Ticket& IOYengine::Ticket::operator=(Ticket && mov){
	std::swap(engine, mov.engine);
	return *this;
}

IOYengine::Ticket IOYengine::ticket(){ return Ticket(this); }

void IOYengine::iothreadwork(){
	auto ioPo = this->ioPo;
	#ifdef _WIN32
	#else
	auto cfdStopReceive = this->cfdStopReceive;
	#endif
	while(true){
		#ifdef _WIN32
		IOCompletionInfo inf;
		ULONG_PTR key;
		LPOVERLAPPED overl;
		inf.status = GetQueuedCompletionStatus(ioPo->rh, &inf.transferred, &key, &overl, INFINITE);
		inf.lerr = GetLastError();
		if(key == COMPLETION_KEY_SHUTDOWN) break;
		if(key == COMPLETION_KEY_IO) reinterpret_cast<IResource::Overlapped*>(overl)->resource->notify(inf);
		#else
		::epoll_event event;
		auto es = ::epoll_wait(ioPo->rh, &event, 1, -1);
		if(es < 0) switch(errno){
			case EINTR: break;
			default: throw std::runtime_error(printSysError("Epoll wait failed"));
		}
		else if(es == 0 || event.data.ptr == this) break;
		else reinterpret_cast<IResource*>(event.data.ptr)->notify(event.events);
		#endif
	}
}

IOResource IOYengine::taek(HandledResource rh){
	std::shared_ptr<FileResource> r(new FileResource(this, std::move(rh)));
	r->setSelf(r);
	return r;
}

//FIO

FileOpenResult fileOpenRead(IOYengine* engine, const std::string& path){
	ResourceHandle file;
	#ifdef _WIN32
	file = CreateFileA(path.c_str(), GENERIC_READ, 0, NULL, OPEN_ALWAYS, FILE_FLAG_OVERLAPPED/* | FILE_FLAG_NO_BUFFERING cf https://docs.microsoft.com/en-us/windows/win32/fileio/file-buffering?redirectedfrom=MSDN */, NULL);
	if(file == INVALID_HANDLE_VALUE) retSysError<FileOpenResult>("Open File failed", GetLastError());
	#else
	file = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if(file < 0) retSysError<FileOpenResult>("Open file failed", errno);
	#endif
	return engine->taek(HandledResource(new StandardHandledResource(file)));
}
FileOpenResult fileOpenWrite(IOYengine* engine, const std::string& path){
	ResourceHandle file;
	#ifdef _WIN32
	file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_FLAG_OVERLAPPED, NULL);
	if(file == INVALID_HANDLE_VALUE) retSysError<FileOpenResult>("Open File failed", GetLastError());
	#else
	file = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NONBLOCK | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if(file < 0) retSysError<FileOpenResult>("Open file failed", errno);
	#endif
	return engine->taek(HandledResource(new StandardHandledResource(file)));
}

}
