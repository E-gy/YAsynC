#pragma once

#include <string>

#ifdef _WIN32
#include <windows.h>
using syserr_t = DWORD;
#else
using syserr_t = int;
#endif

std::string printSysError(const std::string& message, syserr_t e);
std::string printSysError(const std::string& message);
template<typename R> R retSysError(const std::string& message, syserr_t e){ return R::Err(printSysError(message, e)); }
template<typename R> R retSysError(const std::string& message){ return R::Err(printSysError(message)); }
