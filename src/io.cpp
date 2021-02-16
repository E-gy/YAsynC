#include "io.hpp"
#include <stdexcept>
#include <array>
#include "impls.hpp"

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
Yengine* IOYengine::yengine() const { return engine; }

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
		void PrintLastError(DWORD lerr){
			LPSTR err;
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_IGNORE_INSERTS, NULL, lerr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&err, 0, NULL);
			std::cerr << "Uh oh - " << err;
			LocalFree(err);
			throw std::runtime_error("uh oh :("); //FIXME no! Use. Results.
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

#endif
