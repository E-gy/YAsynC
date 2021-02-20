#include "iosock.hpp"

namespace yasync::io {

NetworkedAddressInfo::NetworkedAddressInfo(::addrinfo* ads) : addresses(ads) {}
NetworkedAddressInfo::~NetworkedAddressInfo(){
	::freeaddrinfo(addresses);
}

result<NetworkedAddressInfo, std::string> NetworkedAddressInfo::find(const std::string& node, const std::string& service, const ::addrinfo& hints){
	::addrinfo* ads;
	auto err = ::getaddrinfo(node.c_str(), service.c_str(), &hints, &ads);
	if(err) return result<NetworkedAddressInfo, std::string>::Err(std::string(::gai_strerror(err)));
	return NetworkedAddressInfo(ads);
}

#ifdef _WIN32
SystemNetworkingStateControl::SystemNetworkingStateControl(){
	WSADATA Wsa = {};
	WSAStartup(MAKEWORD(2,2), &Wsa);
}
SystemNetworkingStateControl::~SystemNetworkingStateControl(){
	WSACleanup();
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
