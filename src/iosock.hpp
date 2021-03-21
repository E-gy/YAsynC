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
		template<int SDomain, int SType, int SProto> static inline ::addrinfo hint(){
			::addrinfo hints = {};
			hints.ai_family = SDomain;
			hints.ai_socktype = SType;
			hints.ai_protocol = SProto;
			return hints;
		}
		template<int SDomain, int SType, int SProto> static FindResult find(const std::string& address, const std::string& port){ return find(address, port, hint<SDomain, SType, SProto>()); }
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
			LPFN_ACCEPTEX AcceptEx;
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
			engine.release();
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
			engine->engine->notify(engif);
		}
		void notify(ListenEventType e){ notify(ListenEvent{e, 0}); }
		#ifdef _WIN32
		void notify(syserr_t e){ notify(ListenEvent{ListenEventType::Error, e}); }
		#endif
		syserr_t lerr;
		void notify(IOCompletionInfo inf) override {
			#ifdef _WIN32
			if(!inf.status) notify(inf.lerr);
			else notify(ListenEventType::Accept);
			#else
			if(inf == EPOLLIN) notify(ListenEventType::Accept);
			else notify(ListenEvent{ListenEventType::Error, errno});
			#endif
		}
		void cancel() override {} //Doesn't make much sense...	Use shutdown to stop listening.
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
		IOYengine::Ticket engine;
		AddressInfo address;
		AListeningSocket(IOYengine* e, SocketHandle socket, Errs era, Acc accept) : sock(socket), engif(new OutsideFuture<ListenEvent>()), erracc(era), acceptor(accept), engine(e->ticket()) {
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
			if(!::SetFileCompletionNotificationModes(reinterpret_cast<HANDLE>(sock), FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)) return retSysError<ListenResult>("ioCP set notification mode failed");
			#else
			// return retSysNetError<ListenResult>("listen failed");
			{
				::epoll_event epm;
				epm.events = EPOLLIN|EPOLLONESHOT;
				epm.data.ptr = this;
				if(::epoll_ctl(engine->ioPo->rh, EPOLL_CTL_ADD, sock, &epm) < 0) return retSysError<ListenResult>("epoll add failed");
			}
			#endif
			return ListenResult::Ok(engine->engine->launch(lambdagen([this, self = slf.lock()](const Yengine*, bool& done, int) -> Generesume<void> {
				if(done) return monoid<void>();
				auto stahp = [&](){
					done = true;
					close();
					return monoid<void>();
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
					if(SystemNetworkingStateControl::MSWSA.AcceptEx(sock, lconn->sock(), &linterloc, 0, sizeof(linterloc.local), sizeof(linterloc.remote), &reclen, overlapped())){
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
				return AFuture(engif);
			}, 0)));
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
	int reua = 1;
	if(::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<void*>(&reua), sizeof(reua)) < 0) return retSysError<result<LSock, std::string>>("socket set reuse address failed");
	int fsf = fcntl(sock, F_GETFL, 0);
	if(fsf < 0) return retSysError<result<LSock, std::string>>("socket get flags failed"); 
	if(fcntl(sock, F_SETFL, fsf|O_NONBLOCK) < 0) return retSysError<result<LSock, std::string>>("socket set non-blocking failed");
	#endif
	return result<LSock, std::string>::Ok(LSock(new AListeningSocket<SDomain, SType, SProto, AddressInfo, Errs, Acc>(engine, sock, erracc, acceptor)));
}

using ConnectionResult = result<IOResource, std::string>;

class ConnectingSocket : public IResource {
	IOYengine::Ticket engine;
	HandledStrayIOSocket sock;
	void notify(IOCompletionInfo inf) override;
	public:
		//exposed exclusively for `netConnectTo`
		using ConnRedyResult = result<void, std::string>;
		std::shared_ptr<OutsideFuture<ConnRedyResult>> redy;
		std::weak_ptr<ConnectingSocket> slf;
		ConnectingSocket(IOYengine* e, HandledStrayIOSocket && s) : engine(e->ticket()), sock(std::move(s)), redy(new OutsideFuture<ConnRedyResult>()) {}
		//
		void cancel() override;
		/**
		 * Returns future that completes when connection is established
		 */
		Future<ConnectionResult> connest();
};

template<int SDomain, int SType, int SProto, typename AddressInfo> result<std::shared_ptr<ConnectingSocket>, std::string> netConnectTo(IOYengine* engine, const NetworkedAddressInfo* addri){
	using Result = result<std::shared_ptr<ConnectingSocket>, std::string>;
	SocketHandle sock;
	#ifdef _WIN32
	sock = ::WSASocket(SDomain, SType, SProto, NULL, 0, WSA_FLAG_OVERLAPPED);
	if(sock == INVALID_SOCKET) return retSysError<Result>("WSA socket construction failed");
	AddressInfo winIniBindTo = {};
	auto bind0 = reinterpret_cast<::sockaddr*>(&winIniBindTo);
	bind0->sa_family = SDomain;
	if(::bind(sock, bind0, sizeof(AddressInfo)) < 0) return retSysError<Result>("WSA bind failed");
	#else
	sock = ::socket(SDomain, SType, SProto);
	if(sock < 0) return retSysError<Result>("socket construction failed");
	#endif
	auto csock = std::make_shared<ConnectingSocket>(engine, std::make_unique<AHandledStrayIOSocket>(sock, true));
	csock->slf = csock;
	#ifdef _WIN32
	if(!::CreateIoCompletionPort(reinterpret_cast<HANDLE>(sock), engine->ioPo->rh, COMPLETION_KEY_IO, 0)) return retSysError<Result>("ioCP add failed");
	if(!::SetFileCompletionNotificationModes(reinterpret_cast<HANDLE>(sock), FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)) return retSysError<Result>("ioCP set notification mode failed");
	#else
	int fsf = ::fcntl(sock, F_GETFL, 0);
	if(fsf < 0) return retSysError<Result>("socket get flags failed");
	if(::fcntl(sock, F_SETFL, fsf|O_NONBLOCK) < 0) return retSysError<Result>("socket set non-blocking failed");
	{
		::epoll_event epm;
		epm.events = EPOLLOUT|EPOLLONESHOT;
		epm.data.ptr = csock.get();
		if(::epoll_ctl(engine->ioPo->rh, EPOLL_CTL_ADD, sock, &epm) < 0) return retSysError<Result>("epoll add failed");
	}
	#endif
	auto candidate = addri->addresses;
	for(; candidate; candidate = candidate->ai_next){
		#ifdef _WIN32
		if(SystemNetworkingStateControl::MSWSA.ConnectEx(sock, candidate->ai_addr, candidate->ai_addrlen, NULL, 0, NULL, csock->overlapped()))
		#else
		if(::connect(sock, candidate->ai_addr, candidate->ai_addrlen) == 0)
		#endif
		{
			csock->redy->s = FutureState::Completed;
			csock->redy->r = ConnectingSocket::ConnRedyResult::Ok();
			break;
		}
		#ifdef _WIN32
		if(WSAGetLastError() == ERROR_IO_PENDING)
		#else
		if(errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS)
		#endif
			break; //async connect, cross your fingers it succeeds. otherwise, and if there're remaining candidates, we're in deep quack
	}
	if(!candidate) return Result::Err("Exhausted address space");
	return csock;
}

}
