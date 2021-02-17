#include "iosock.hpp"

namespace yasync::io {

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
~SystemNetworkingStateControl::SystemNetworkingStateControl(){}
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
