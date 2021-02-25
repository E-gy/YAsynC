#include "iosock.hpp"

#include <iostream>

namespace yasync::io {

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

}
