#include "ioctrlc.hpp"

#include "impls.hpp"
#include "io.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#endif

namespace yasync::io {

static bool stahp = false;

#ifdef _WIN32
static ResourceHandle ctrlcEvent = INVALID_HANDLE_VALUE;
BOOL WINAPI ctrlcHandler(DWORD sig){
	if(ctrlcEvent == INVALID_HANDLE_VALUE) return FALSE;
	switch(sig){
		case CTRL_C_EVENT:
		case CTRL_CLOSE_EVENT:
			::SetEvent(ctrlcEvent);
			return TRUE;
		default: return FALSE;
    }
}
void CtrlC::setup(){}
#else
void CtrlC::setup(){
	sigset_t sigs;
	::sigemptyset(&sigs);
	::sigaddset(&sigs, SIGINT);
	::pthread_sigmask(SIG_BLOCK, &sigs, NULL);
}
#endif

result<Future<void>, std::string> CtrlC::on(Yengine* engine){
	stahp = false;
	#ifdef _WIN32
	if(ctrlcEvent != INVALID_HANDLE_VALUE) return result<Future<void>, std::string>::Err("ctrl+c handler already set!");
	ctrlcEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!::SetConsoleCtrlHandler(ctrlcHandler, true)) return retSysError<result<Future<void>, std::string>>("Set ctrl+c handler failed");
	#else
	sigset_t sigs;
	::sigemptyset(&sigs);
	::sigaddset(&sigs, SIGINT);
	#endif
	std::shared_ptr<OutsideFuture<void>> n(new OutsideFuture<void>());
	Daemons::launch([=](){
		while(true){
			#ifdef _WIN32
			::WaitForSingleObject(ctrlcEvent, INFINITE);
			#else
			int sig;
			::sigwait(&sigs, &sig);
			#endif
			if(stahp) break;
			n->s = FutureState::Completed;
			engine->notify(std::static_pointer_cast<IFutureT<void>>(n));
		}
		stahp = false;
	});
	return result<Future<void>, std::string>::Ok(n);
}
void CtrlC::un(){
	#ifdef _WIN32
	if(ctrlcEvent == INVALID_HANDLE_VALUE) return;
	#else
	#endif
	stahp = true;
	#ifdef _WIN32
	::SetEvent(ctrlcEvent);
	ResourceHandle c = INVALID_HANDLE_VALUE;
	std::swap(ctrlcEvent, c);
	::CloseHandle(c);
	//SetConsoleCtrlHandler(NULL, false);
	#else
	// struct sigaction sigh = {};
	// sigh.sa_handler = SIG_DFL;
	// ::sigemptyset(&sigh.sa_mask);
	// ::sigaction(SIGINT, &sigh, NULL);
	#endif
}

result<void, std::string> mainThreadWaitCtrlC(){
	#ifdef _WIN32
	if(ctrlcEvent != INVALID_HANDLE_VALUE) return result<void, std::string>::Err("ctrl+c handler already set!");
	ctrlcEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!SetConsoleCtrlHandler(ctrlcHandler, true)) return retSysError<result<void, std::string>>("Set ctrl+c handler failed");
	WaitForSingleObject(ctrlcEvent, INFINITE);
	CloseHandle(ctrlcEvent);
	ctrlcEvent = INVALID_HANDLE_VALUE;
	#else
	sigset_t sigs;
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGINT);
	int sig = 0;
	sigwait(&sigs, &sig);
	#endif
	return result<void, std::string>::Ok();
}

}
