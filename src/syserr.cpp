#include "syserr.hpp"

#include <sstream>
#include <cstring>

std::string printSysError(const std::string& message, syserr_t e){
	std::ostringstream compose;
	compose << message << ": ";
	#ifdef _WIN32
	LPSTR err;
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&err, 0, NULL);
	compose << err;
	LocalFree(err);
	#else
	compose << std::strerror(e); //FIXME not thread safe!
	// if(e < ::sys_nerr) compose << ::sys_errlist[e];
	// else compose << "Unknown error " << e;
	#endif
	return compose.str();
}
std::string printSysError(const std::string& message){
	return printSysError(message,
	#ifdef _WIN32
	::GetLastError()
	#else
	errno
	#endif
	);
}
