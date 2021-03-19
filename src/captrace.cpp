#include "captrace.hpp"

#ifdef _DEBUG
#include "syserr.hpp"
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#else
#include <execinfo.h>
#endif
#include <iostream>
#include <mutex>

void TraceCapture::setup(){
	#ifdef _WIN32
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
	HANDLE proc = GetCurrentProcess();
	if(!SymInitialize(proc, NULL, TRUE)) std::cerr << printSysError("dbg sym init error");
	#endif
}

TraceCapture::TraceCapture(unsigned upto){
	trace.reserve(upto);
	auto stackSize =
	#ifdef _WIN32
	#include <windows.h>
	::CaptureStackBackTrace(0, upto, trace.data(), NULL);
	#else
	::backtrace(trace.data(), upto);
	#endif
	trace.resize(stackSize);
	if(trace.size() > 0) trace.erase(trace.begin());
}

std::ostream& operator<<(std::ostream& os, const TraceCapture& trace){
	#ifdef _WIN32
	static std::mutex dbgsynch;
	std::unique_lock lock(dbgsynch);
	HANDLE proc = GetCurrentProcess();
	char _ssymbol[sizeof(SYMBOL_INFO)*3] = {};
	SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(_ssymbol);
	symbol->SizeOfStruct = sizeof(*symbol);
	symbol->MaxNameLen = sizeof(*symbol)*2;
	DWORD64 displ = 0;
	for(unsigned i = 0; i < trace.trace.size(); i++){
		os << "[" << i << "]: ";
		if(::SymFromAddr(proc, reinterpret_cast<DWORD64>(trace.trace[i]), &displ, symbol)) os << symbol->Name << " - " << reinterpret_cast<void*>(symbol->Address) << "\n";
		else os << printSysError("<error retrieving symbol>");
	}
	#else
	char** symbols = ::backtrace_symbols(trace.trace.data(), trace.trace.size());
	for(unsigned i = 0; i < trace.trace.size(); i++) os << symbols[i] << "\n";
	::free(symbols);
	#endif
	return os;
}

#else

void TraceCapture::setup(){}
TraceCapture::TraceCapture(unsigned){}
std::ostream& operator<<(std::ostream& os, const TraceCapture&){ return os << "{missing-trace}\n"; }

#endif
