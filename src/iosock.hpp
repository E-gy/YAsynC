#ifdef _WIN32
#include <winsock2.h> //Please include winsock2.h before windows.h
#endif

#include "io.hpp"

#ifdef _WIN32
#include <mswsock.h>
#include <ws2tcpip.h>
#else
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netdb.h>
#endif

namespace yasync::io {

class NetworkedAddressInfo {
	NetworkedAddressInfo(::addrinfo* ads);
	public:
		::addrinfo* addresses;
		NetworkedAddressInfo(const NetworkedAddressInfo&) = delete;
		NetworkedAddressInfo& operator=(const NetworkedAddressInfo&) = delete;
		NetworkedAddressInfo(NetworkedAddressInfo &&);
		NetworkedAddressInfo& operator=(NetworkedAddressInfo &&);
		~NetworkedAddressInfo();
		using FindResult = result<NetworkedAddressInfo, std::string>;
		static FindResult find(const std::string& address, const std::string& port, const ::addrinfo& hints);
};

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
		#ifdef _WIN32
		struct mswsock {
			LPFN_CONNECTEX ConnectEx;
		};
		static mswsock MSWSA;
		#endif
};

template<int SDomain, int SType, int SProto, typename AddressInfo, typename Errs, typename Acc> class AListeningSocket;
template<int SDomain, int SType, int SProto, typename AddressInfo, typename Errs, typename Acc> using ListeningSocket = std::shared_ptr<AListeningSocket<SDomain, SType, SProto, AddressInfo, Errs, Acc>>;

class AHandledStrayIOSocket : public IHandledResource {
	public:
		inline SocketHandle sock() const { return SocketHandle(rh); }
		AHandledStrayIOSocket(SocketHandle sock, bool iopor = false) : IHandledResource(ResourceHandle(sock), iopor){}
		~AHandledStrayIOSocket(){
			#ifdef _WIN32
			if(sock() != INVALID_SOCKET){
				::shutdown(sock(), SD_BOTH);
				::closesocket(sock());
			}
			#else
			if(sock() >= 0){
				::shutdown(sock(), SHUT_RDWR);
				::close(sock());
			}
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
			if(sock != INVALID_SOCKET) ::closesocket(sock);
			sock = INVALID_SOCKET;
			lconn.reset();
			#else
			if(sock >= 0) ::close(sock);
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
		#ifdef _WIN32
		void notify(syserr_t e){ notify(ListenEvent{ListenEventType::Error, e}); }
		#endif
		syserr_t lerr;
		void notify(IOCompletionInfo inf){
			#ifdef _WIN32
			if(!inf.status) notify(inf.lerr);
			else notify(ListenEventType::Accept);
			#else
			if(inf == EPOLLIN) notify(ListenEventType::Accept);
			else notify(ListenEvent{ListenEventType::Error, errno});
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
		AddressInfo address;
		AListeningSocket(IOYengine* e, SocketHandle socket, Errs era, Acc accept) : sock(socket), engif(new OutsideFuture<ListenEvent>()), erracc(era), acceptor(accept), engine(e) {
			#ifdef _WIN32
			
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
		ListenResult listen(const NetworkedAddressInfo* addri){
			{
				auto candidate = addri->addresses;
				//https://stackoverflow.com/a/50227324
				for(; candidate; candidate = candidate->ai_next) if(::bind(sock, candidate->ai_addr, candidate->ai_addrlen) == 0) if(::listen(sock, 200) == 0) break;
				if(!candidate) return ListenResult::Err("Exhausted address space");
				address = *reinterpret_cast<AddressInfo*>(candidate->ai_addr);
			}
			#ifdef _WIN32
			// if(::listen(sock, 200) == SOCKET_ERROR) return retSysNetError<ListenResult>("WSA listen failed");
			if(!CreateIoCompletionPort(reinterpret_cast<HANDLE>(sock), engine->ioPo->rh, COMPLETION_KEY_IO, 0)) return retSysError<ListenResult>("ioCP add failed");
			#else
			// return retSysNetError<ListenResult>("listen failed");
			{
				::epoll_event epm;
				epm.events = EPOLLIN|EPOLLONESHOT;
				epm.data.ptr = this;
				if(::epoll_ctl(engine->ioPo->rh, EPOLL_CTL_ADD, sock, &epm) < 0) return retSysError<ListenResult>("epoll add failed");
			}
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
							bool goAsync = false;
							while(!goAsync){
								AddressInfo remote;
								socklen_t remlen = sizeof(remote); 
								while(true){
									auto conn = ::accept(sock, reinterpret_cast<sockaddr*>(&remote), &remlen);
									if(conn < 0) break;
									acceptor(remote, engine->taek(HandledResource(HandledStrayIOSocket(new AHandledStrayIOSocket(conn)))));
								}
								switch(errno){
									#if EAGAIN != EWOULDBLOCK
									case EWOULDBLOCK:
									#endif
									case EAGAIN:
										goAsync = true;
										break;
									case ECONNABORTED: break;
									default: if(erracc(self, errno, "accept failed")) return stahp();
								}
							}
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
				{
					::epoll_event epm;
					epm.events = EPOLLIN|EPOLLONESHOT;
					epm.data.ptr = this;
					if(::epoll_ctl(engine->ioPo->rh, EPOLL_CTL_MOD, sock, &epm) < 0) if(erracc(self, errno, "EPoll rearm failed")) return stahp();
				}
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

template<int SDomain, int SType, int SProto, typename AddressInfo, typename Errs, typename Acc> result<ListeningSocket<SDomain, SType, SProto, AddressInfo, Errs, Acc>, std::string> netListen(IOYengine* engine, Errs erracc, Acc acceptor){
	using LSock = ListeningSocket<SDomain, SType, SProto, AddressInfo, Errs, Acc>;
	SocketHandle sock;
	#ifdef _WIN32
	sock = ::WSASocket(SDomain, SType, SProto, NULL, 0, WSA_FLAG_OVERLAPPED);
	if(sock == INVALID_SOCKET) return retSysError<result<LSock, std::string>>("WSA socket construction failed");
	#else
	sock = ::socket(SDomain, SType, SProto);
	if(sock < 0) return retSysError<result<LSock, std::string>>("socket construction failed");
	int fsf = fcntl(sock, F_GETFL, 0);
	if(fsf < 0) return retSysError<result<LSock, std::string>>("socket get flags failed"); 
	if(fcntl(sock, F_SETFL, fsf|O_NONBLOCK) < 0) return retSysError<result<LSock, std::string>>("socket set non-blocking failed");
	#endif
	return result<LSock, std::string>::Ok(LSock(new AListeningSocket<SDomain, SType, SProto, AddressInfo, Errs, Acc>(engine, sock, erracc, acceptor)));
}

using ConnectionResultSock = result<HandledStrayIOSocket, std::string>;
using ConnectionResult = result<IOResource, std::string>;

class ConnectingSocket : public IResource {
	public:
		IOYengine* engine;
		HandledStrayIOSocket sock;
		std::shared_ptr<OutsideFuture<ConnectionResultSock>> redy;
		ConnectingSocket(IOYengine* e, HandledStrayIOSocket && s) : engine(e), sock(std::move(s)), redy(new OutsideFuture<ConnectionResultSock>()) {}
		void notify(IOCompletionInfo inf) override {
			redy->s = FutureState::Completed;
			#ifdef _WIN32
			if(inf.status) redy->r = ConnectionResultSock::Ok(std::move(sock));
			else redy->r = retSysNetError<ConnectionResultSock>("ConnectEx async failed", inf.lerr);
			#else
			if(inf == EPOLLOUT) redy->r = ConnectionResultSock::Ok(std::move(sock));
			else redy->r = retSysNetError<ConnectionResultSock>("connect async failed");
			#endif
			engine->engine->notify(std::static_pointer_cast<IFutureT<ConnectionResultSock>>(redy));
		}
};

template<int SDomain, int SType, int SProto, typename AddressInfo> result<Future<ConnectionResult>, std::string> netConnectTo(IOYengine* engine, const NetworkedAddressInfo* addri, AddressInfo winIniBindTo){
	using Result = result<Future<ConnectionResult>, std::string>;
	SocketHandle sock;
	HandledStrayIOSocket hsock; 
	#ifdef _WIN32
	sock = ::WSASocket(SDomain, SType, SProto, NULL, 0, WSA_FLAG_OVERLAPPED);
	if(sock == INVALID_SOCKET) return retSysError<Result>("WSA socket construction failed");
	if(::bind(sock, reinterpret_cast<const sockaddr*>(&winIniBindTo), sizeof(AddressInfo)) < 0) return retSysError<Result>("WSA bind failed");
	#else
	sock = ::socket(SDomain, SType, SProto);
	if(sock < 0) return retSysError<Result>("socket construction failed");
	#endif
	hsock = HandledStrayIOSocket(new AHandledStrayIOSocket(sock));
	#ifdef _WIN32
	if(!::CreateIoCompletionPort(reinterpret_cast<HANDLE>(sock), engine->ioPo->rh, COMPLETION_KEY_IO, 0)) return retSysError<Result>("ioCP add failed");
	#else
	int fsf = ::fcntl(sock, F_GETFL, 0);
	if(fsf < 0) return retSysError<Result>("socket get flags failed");
	if(::fcntl(sock, F_SETFL, fsf|O_NONBLOCK) < 0) return retSysError<Result>("socket set non-blocking failed");
	{
		::epoll_event epm;
		epm.events = EPOLLOUT|EPOLLONESHOT;
		epm.data.ptr = this;
		if(::epoll_ctl(engine->ioPo->rh, EPOLL_CTL_ADD, sock, &epm) < 0) return retSysError<Result>("epoll add failed");
	}
	#endif
	hsock->iopor = true;
	auto csock = std::shared_ptr<ConnectingSocket>(new ConnectingSocket(engine, std::move(hsock)));
	auto candidate = addri->addresses;
	for(; candidate; candidate = candidate->ai_next){
		#ifdef _WIN32
		if(SystemNetworkingStateControl::MSWSA.ConnectEx(sock, candidate->ai_addr, candidate->ai_addrlen, NULL, 0, NULL, csock->overlapped()))
		#else
		if(::connect(sock, candidate->ai_addr, candidate->ai_addrlen) == 0)
		#endif
		{
			csock->redy->s = FutureState::Completed;
			csock->redy->r = ConnectionResultSock::Ok(std::move(csock->sock));
			break;
		}
		#ifdef _WIN32
		if(WSAGetLastError() == ERROR_IO_PENDING)
		#else
		if(errno == EWOULDBLOCK || errno == EAGAIN)
		#endif
			break; //async connect, cross your fingers it succeeds. otherwise, and if there're remaining candidates, we're in deep quack
	}
	if(!candidate) return Result::Err("Exhausted address space");
	return std::static_pointer_cast<IFutureT<ConnectionResultSock>>(csock->redy) >> [engine, csock](ConnectionResultSock res){ //we need to grab `csock` so it lives until outside handover lul
		if(auto ok = res.ok()){
			#ifdef _WIN32
			if(::setsockopt((*ok)->sock(), SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0)) return retSysNetError<ConnectionResult>("update context connect failed");
			#endif
			return ConnectionResult::Ok(engine->taek(HandledResource(ok->release())));
		}
		return ConnectionResult::Err(*res.err());
	};
}

}
