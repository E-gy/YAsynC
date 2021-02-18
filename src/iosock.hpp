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
template<typename S> result<S, std::string> retSysNetError(const std::string& message, sysneterr_t e){ return RError<S, std::string>(printSysNetError(message, e)); }
template<typename S> result<S, std::string> retSysNetError(const std::string& message){ return RError<S, std::string>(printSysNetError(message)); }

class SystemNetworkingStateControl {
	public:
		SystemNetworkingStateControl();
		~SystemNetworkingStateControl();
		SystemNetworkingStateControl(const SystemNetworkingStateControl&) = delete;
		SystemNetworkingStateControl(SystemNetworkingStateControl&&) = delete;
};

template<int SDomain, int SType, int SProto, typename AddressInfo, typename Acc> class AListeningSocket;
template<int SDomain, int SType, int SProto, typename AddressInfo, typename Acc> using ListeningSocket = std::shared_ptr<AListeningSocket<SDomain, SType, SProto, AddressInfo, Acc>>;

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
 * @typeparam Acc (AddressInfo, IOResource) -> result<void, std::string>
 */
template<int SDomain, int SType, int SProto, typename AddressInfo, typename Acc> class AListeningSocket : public IResource {		
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
			engif->r.emplace(e);
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
		Acc acceptor;
	public:
		IOYengine* const engine;
		const AddressInfo address;
		AListeningSocket(IOYengine* e, SocketHandle socket, AddressInfo addr, Acc accept) : sock(socket), engif(new OutsideFuture<ListenEvent>()), acceptor(accept), engine(e), address(addr) {
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
		using ListenResult = result<void, std::string>;
		/**
		 * Starts listening
		 * @returns future that will complete when the socket shutdowns, or errors.
		 */
		Future<ListenResult> listen(){
			#ifdef _WIN32 //https://stackoverflow.com/a/50227324
			if(::bind(sock, reinterpret_cast<const sockaddr*>(&address), sizeof(AddressInfo)) == SOCKET_ERROR) return completed<ListenResult>(retSysNetError<void>("WSA bind failed"));
			if(::listen(sock, SOMAXCONN) == SOCKET_ERROR) return completed<ListenResult>(retSysNetError<void>("WSA listen failed"));
			#else
			#endif
			return defer(lambdagen([this, self = slf.lock()]([[maybe_unused]] const Yengine* _engine, bool& done, [[maybe_unused]] int _un) -> std::variant<AFuture, something<ListenResult>>{
				if(done) return ROk<std::string>();
				if(engif->s == FutureState::Completed){
					engif->s = FutureState::Running;
					switch((*engif->r)->type){
						case ListenEventType::Accept:{
							#ifdef _WIN32
							if(::setsockopt(lconn->sock(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<char*>(&sock), sizeof(sock)) == SOCKET_ERROR) return retSysNetError<void>("Set accepting socket accept failed");
							auto takire = acceptor(linterloc.remote.addr, engine->taek(HandledResource(std::move(lconn))));
							if(takire.isError()) return takire;
							#else
							#endif
							break;
						}
						case ListenEventType::Error:
							return retSysNetError<void>("Async accept error", (*engif->r)->err);
						case ListenEventType::Close:
							done = true;
							close();
							return ROk<std::string>();
						default: return RError<void, std::string>("Received unknown event");
					}
				}
				#ifdef _WIN32
				bool goAsync = false;
				while(!goAsync){
					{
						auto nconn = ::WSASocket(SDomain, SType, SProto, NULL, 0, WSA_FLAG_OVERLAPPED);
						if(nconn == INVALID_SOCKET) return retSysNetError<void>("Create accepting socket failed");
						lconn.reset(new AHandledStrayIOSocket(nconn));
					}
					DWORD reclen;
					if(::AcceptEx(sock, lconn->sock(), &linterloc, 0, sizeof(linterloc.local), sizeof(linterloc.remote), &reclen, overlapped())){
						if(::setsockopt(lconn->sock(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<char*>(&sock), sizeof(sock)) == SOCKET_ERROR) return retSysNetError<void>("Set accepting socket accept failed");
						auto takire = acceptor(linterloc.remote.addr, engine->taek(HandledResource(std::move(lconn))));
						if(takire.isError()) return takire;
					}
					else switch(::WSAGetLastError()){
						case WSAEWOULDBLOCK:
						case ERROR_IO_PENDING:
							goAsync = true;
							break;
						case WSAECONNRESET: break;
						default: return retSysNetError<void>("Accept failed");
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

template<int SDomain, int SType, int SProto, typename AddressInfo, typename Acc> result<ListeningSocket<SDomain, SType, SProto, AddressInfo, Acc>, std::string> netListen(IOYengine* engine, AddressInfo address, Acc acceptor){
	SocketHandle sock;
	#ifdef _WIN32
	sock = ::WSASocket(SDomain, SType, SProto, NULL, 0, WSA_FLAG_OVERLAPPED);
	if(sock == INVALID_SOCKET) return retSysError<ListeningSocket<SDomain, SType, SProto, AddressInfo, Acc>>("WSA socket construction failed");
	#else
	//TODO
	#endif
	return ROk<ListeningSocket<SDomain, SType, SProto, AddressInfo, Acc>, std::string>(ListeningSocket<SDomain, SType, SProto, AddressInfo, Acc>(new AListeningSocket<SDomain, SType, SProto, AddressInfo, Acc>(engine, sock, address, acceptor)));
}


}
