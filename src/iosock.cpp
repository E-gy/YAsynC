#include "iosock.hpp"

#include <iostream>

namespace yasync::io {

using namespace magikop;

NetworkedAddressInfo::NetworkedAddressInfo(::addrinfo* ads) : addresses(ads) {}
NetworkedAddressInfo::NetworkedAddressInfo(NetworkedAddressInfo && mov){
	addresses = mov.addresses;
	mov.addresses = nullptr;
}
NetworkedAddressInfo& NetworkedAddressInfo::operator=(NetworkedAddressInfo && mov){
	if(addresses) ::freeaddrinfo(addresses);
	addresses = mov.addresses;
	mov.addresses = nullptr;
	return *this;
}
NetworkedAddressInfo::~NetworkedAddressInfo(){
	if(addresses) ::freeaddrinfo(addresses);
}

NetworkedAddressInfo::FindResult NetworkedAddressInfo::find(const std::string& addr, const std::string& port, const ::addrinfo& hints){
	::addrinfo* ads;
	auto err = ::getaddrinfo(addr.c_str(), port.c_str(), &hints, &ads);
	if(err) return NetworkedAddressInfo::FindResult::Err(std::string(reinterpret_cast<const char*>(::gai_strerror(err))));
	return NetworkedAddressInfo::FindResult::Ok(NetworkedAddressInfo(ads));
}

#ifdef _WIN32
SystemNetworkingStateControl::mswsock SystemNetworkingStateControl::MSWSA = {};
SystemNetworkingStateControl::SystemNetworkingStateControl(){
	WSADATA Wsa = {};
	auto wserr = WSAStartup(MAKEWORD(2,2), &Wsa);
	if(wserr){
		std::cerr << printSysNetError("WSA Startup failed: ", wserr);
		std::terminate();
	}
	SocketHandle dummy = socket(AF_INET, SOCK_STREAM, 0);
	DWORD dummi;
	GUID guid;
	guid = WSAID_CONNECTEX;
	WSAIoctl(dummy, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &MSWSA.ConnectEx, sizeof(MSWSA.ConnectEx), &dummi, NULL, NULL);
	guid = WSAID_ACCEPTEX;
	WSAIoctl(dummy, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &MSWSA.AcceptEx, sizeof(MSWSA.AcceptEx), &dummi, NULL, NULL);
	closesocket(dummy);
	std::cout << "WSA Started successfully\n";
}
SystemNetworkingStateControl::~SystemNetworkingStateControl(){
	WSACleanup();
	std::cout << "WSA Shut down successfully\n";
}
#else
SystemNetworkingStateControl::SystemNetworkingStateControl(){}
SystemNetworkingStateControl::~SystemNetworkingStateControl(){}
#endif

#ifdef _WIN32
std::string printSysNetError(const std::string& message, sysneterr_t e){
	return printSysError(message, syserr_t(e) /* FIXME */);
}
std::string printSysNetError(const std::string& message){ return printSysNetError(message, ::WSAGetLastError()); }
#else
std::string printSysNetError(const std::string& message, sysneterr_t e){ return printSysError(message, e); }
std::string printSysNetError(const std::string& message){ return printSysError(message); }
#endif

void ConnectingSocket::notify(IOCompletionInfo inf){
	redy->s = FutureState::Completed;
	#ifdef _WIN32
	if(inf.status) redy->r = ConnRedyResult::Ok();
	else if(inf.lerr == ERROR_OPERATION_ABORTED) redy->r = ConnRedyResult::Err("Operation cancelled");
	else redy->r = retSysNetError<ConnRedyResult>("ConnectEx async failed", inf.lerr);
	#else
	if((inf & EPOLLHUP) != 0) redy->r = ConnRedyResult::Err("Operation cancelled");
	else if((inf & EPOLLERR) != 0){
		int serr = 0;
		::socklen_t serrlen = sizeof(serr);
		if(::getsockopt(sock->rh, SOL_SOCKET, SO_ERROR, reinterpret_cast<void*>(&serr), &serrlen) < 0) redy->r = retSysNetError<ConnRedyResult>("connect async failed, and trying to understand why failed too");
		else redy->r = retSysNetError<ConnRedyResult>("connect async failed", serr);
	} else if((inf & EPOLLOUT) != 0) redy->r = ConnRedyResult::Ok();
	else redy->r = retSysNetError<ConnRedyResult>("connect async scramble skedable");
	#endif
	engine->engine->notify(redy);
}
void ConnectingSocket::cancel(){
	#ifdef _WIN32
	CancelIoEx(sock->rh, overlapped());
	#else
	notify(EPOLLHUP);
	#endif
}
Future<ConnectionResult> ConnectingSocket::connest(){
	return redy >> ([=, self = slf.lock()](){
		#ifdef _WIN32
		if(::setsockopt(sock->sock(), SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0)) return retSysNetError<ConnectionResult>("update context connect failed");
		#endif
		return ConnectionResult::Ok(engine->taek(HandledResource(sock.release())));	
	}|[](auto err){ return ConnectionResult::Err(err); });
}

}
