#ifdef _WIN32
#include <winsock2.h> //Please include winsock2.h before windows.h
#endif

#include "io.hpp"

#ifdef _WIN32
#include <mswsock.h>
#else
#endif

namespace yasync::io {

#ifdef _WIN32
using SocketHandle = SOCKET;
#else
using SocketHandle = ResourceHandle;
#endif

using sysneterr_t = int;

std::string printSysNetError(const std::string& message, sysneterr_t e);
std::string printSysNetError(const std::string& message);
template<typename R> R retSysNetError(const std::string& message, sysneterr_t e){ return R::Err(printSysNetError(message, e)); }
template<typename R> R retSysNetError(const std::string& message){ return R::Err(printSysNetError(message)); }

class SystemNetworkingStateControl {
	public:
		SystemNetworkingStateControl();
		~SystemNetworkingStateControl();
		SystemNetworkingStateControl(const SystemNetworkingStateControl&) = delete;
		SystemNetworkingStateControl(SystemNetworkingStateControl&&) = delete;
};

template<int SDomain, int SType, int SProto, typename AddressInfo, typename Errs, typename Acc> class AListeningSocket;
template<int SDomain, int SType, int SProto, typename AddressInfo, typename Errs, typename Acc> using ListeningSocket = std::shared_ptr<AListeningSocket<SDomain, SType, SProto, AddressInfo, Errs, Acc>>;

class AHandledStrayIOSocket : public IHandledResource {
	public:
		inline SocketHandle sock() const { return SocketHandle(rh); }
		AHandledStrayIOSocket(SocketHandle sock) : IHandledResource(ResourceHandle(sock)){}
		~AHandledStrayIOSocket(){
			#ifdef _WIN32
			if(sock() != INVALID_SOCKET){
				shutdown(sock(), SD_BOTH);
				closesocket(sock());
			}
			#else
			#endif
		}
};
using HandledStrayIOSocket = std::unique_ptr<AHandledStrayIOSocket>;

/**
 * @typeparam Errs (this, sysneterr_t, string) -> bool
 * @typeparam Acc (AddressInfo, IOResource) -> void
 */
template<int SDomain, int SType, int SProto, typename AddressInfo, typename Errs, typename Acc> class AListeningSocket : public IResource {		
	protected:
		SocketHandle sock;
		std::weak_ptr<AListeningSocket> slf;
		auto setSelf(std::shared_ptr<AListeningSocket> self){ return slf = self; }
		void close(){
			#ifdef _WIN32
			if(sock != INVALID_SOCKET) closesocket(sock);
			sock = INVALID_SOCKET;
			lconn.reset();
			#else
			if(sock >= 0) close(sock);
			sock = -1;
			#endif
		}
	private:
		enum class ListenEventType {
			Close, Accept, Error
		};
		struct ListenEvent {
			ListenEventType type;
			syserr_t err;
		};
		std::shared_ptr<OutsideFuture<ListenEvent>> engif;
		void notify(ListenEvent e){
			engif->r = e;
			engif->s = FutureState::Completed;
			engine->engine->notify(std::dynamic_pointer_cast<IFutureT<ListenEvent>>(engif));
		}
		void notify(ListenEventType e){ notify(ListenEvent{e, 0}); }
		void notify(syserr_t e){ notify(ListenEvent{ListenEventType::Error, e}); }
		syserr_t lerr;
		void notify(IOCompletionInfo inf){
			#ifdef _WIN32
			if(!inf.status) notify(inf.lerr);
			else notify(ListenEventType::Accept);
			#else
			if(inf) notify(inf);
			else notify(ListenEventType::Accept);
			#endif
		}
		#ifdef _WIN32
		HandledStrayIOSocket lconn;
		struct InterlocInf {
			struct PadAddr {
				AddressInfo addr;
				BYTE __reserved[16];
			};
			PadAddr local;
			PadAddr remote;
		};
		InterlocInf linterloc = {};
		#endif
		Errs erracc;
		Acc acceptor;
	public:
		IOYengine* const engine;
		const AddressInfo address;
		AListeningSocket(IOYengine* e, SocketHandle socket, AddressInfo addr, Errs era, Acc accept) : sock(socket), engif(new OutsideFuture<ListenEvent>()), erracc(era), acceptor(accept), engine(e), address(addr) {
			#ifdef _WIN32
			CreateIoCompletionPort(reinterpret_cast<HANDLE>(sock), engine->ioCompletionPort, COMPLETION_KEY_IO, 0);
			#else
			#endif
		}
		AListeningSocket(const AListeningSocket& cpy) = delete;
		AListeningSocket(AListeningSocket&& mv) = delete;
		~AListeningSocket(){
			close();
		}
		using ListenResult = result<Future<void>, std::string>;
		/**
		 * Starts listening
		 * @returns future that will complete when the socket shutdowns, or errors.
		 */
		ListenResult listen(){
			#ifdef _WIN32 //https://stackoverflow.com/a/50227324
			if(::bind(sock, reinterpret_cast<const sockaddr*>(&address), sizeof(AddressInfo)) == SOCKET_ERROR) return retSysNetError<ListenResult>("WSA bind failed");
			if(::listen(sock, SOMAXCONN) == SOCKET_ERROR) return retSysNetError<ListenResult>("WSA listen failed");
			#else
			#endif
			return defer(lambdagen([this, self = slf.lock()]([[maybe_unused]] const Yengine* _engine, bool& done, [[maybe_unused]] int _un) -> std::variant<AFuture, movonly<void>>{
				if(done) return movonly<void>();
				auto stahp = [&](){
					done = true;
					close();
					return movonly<void>();
				};
				if(engif->s == FutureState::Completed){
					auto event = engif->result();
					engif->s = FutureState::Running;
					switch(event->type){
						case ListenEventType::Accept:{
							#ifdef _WIN32
							if(::setsockopt(lconn->sock(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<char*>(&sock), sizeof(sock)) == SOCKET_ERROR){
								if(erracc(self, ::WSAGetLastError(), "Set accepting socket accept failed")) return stahp();
							} else acceptor(linterloc.remote.addr, engine->taek(HandledResource(std::move(lconn))));
							#else
							#endif
							break;
						}
						case ListenEventType::Error:
							if(erracc(self, event->err, "Async accept error")) return stahp();
							break;
						case ListenEventType::Close: return stahp();
						default: if(erracc(self, 0, "Received unknown event")) return stahp();
					}
				}
				#ifdef _WIN32
				bool goAsync = false;
				while(!goAsync){
					{
						auto nconn = ::WSASocket(SDomain, SType, SProto, NULL, 0, WSA_FLAG_OVERLAPPED);
						if(nconn == INVALID_SOCKET) if(erracc(self, ::WSAGetLastError(), "Create accepting socket failed") || true) return stahp();
						lconn.reset(new AHandledStrayIOSocket(nconn));
					}
					DWORD reclen;
					if(::AcceptEx(sock, lconn->sock(), &linterloc, 0, sizeof(linterloc.local), sizeof(linterloc.remote), &reclen, overlapped())){
						if(::setsockopt(lconn->sock(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<char*>(&sock), sizeof(sock)) == SOCKET_ERROR){
							if(erracc(self, ::WSAGetLastError(), "Set accepting socket accept failed")) return stahp();
						} else acceptor(linterloc.remote.addr, engine->taek(HandledResource(std::move(lconn))));
					}
					else switch(::WSAGetLastError()){
						case WSAEWOULDBLOCK:
						case ERROR_IO_PENDING:
							goAsync = true;
							break;
						case WSAECONNRESET: break;
						default: if(erracc(self, ::WSAGetLastError(), "Accept failed")) return stahp();
					}
				}
				#else
				#endif
				return engif;
			}, 0));
		}
		/**
		 * Initiates shutdown sequence.
		 * No new connections will be accepted from this point on. 
		 */
		void shutdown(){
			notify(ListenEventType::Close);
		}
};

template<int SDomain, int SType, int SProto, typename AddressInfo, typename Errs, typename Acc> result<ListeningSocket<SDomain, SType, SProto, AddressInfo, Errs, Acc>, std::string> netListen(IOYengine* engine, AddressInfo address, Errs erracc, Acc acceptor){
	using LSock = ListeningSocket<SDomain, SType, SProto, AddressInfo, Errs, Acc>;
	SocketHandle sock;
	#ifdef _WIN32
	sock = ::WSASocket(SDomain, SType, SProto, NULL, 0, WSA_FLAG_OVERLAPPED);
	if(sock == INVALID_SOCKET) return retSysError<result<LSock, std::string>>("WSA socket construction failed");
	#else
	//TODO
	#endif
	return result<LSock, std::string>::Ok(LSock(new AListeningSocket<SDomain, SType, SProto, AddressInfo, Errs, Acc>(engine, sock, address, erracc, acceptor)));
}


}
