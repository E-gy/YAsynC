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
			SetEvent(ctrlcEvent);
			return TRUE;
		default: return FALSE;
    }
}
#else
#endif

result<Future<void>, std::string> onCtrlC(Yengine* engine){
	stahp = false;
	#ifdef _WIN32
	if(ctrlcEvent != INVALID_HANDLE_VALUE) return result<Future<void>, std::string>::Err("ctrl+c handler already set!");
	ctrlcEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!SetConsoleCtrlHandler(ctrlcHandler, true)) return retSysError<result<Future<void>, std::string>>("Set ctrl+c handler failed");
	#else
	sigset_t sigs;
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGINT);
	#endif
	std::shared_ptr<OutsideFuture<void>> n(new OutsideFuture<void>());
	std::thread sigh([=](){
		while(true){
			#ifdef _WIN32
			WaitForSingleObject(ctrlcEvent, INFINITE);
			#else
			int sig;
			sigwait(&sigs, &sig);
			#endif
			if(stahp) break;
			n->s = FutureState::Completed;
			engine->notify(std::static_pointer_cast<IFutureT<void>>(n));
		}
		stahp = false;
	});
	sigh.detach();
	return result<Future<void>, std::string>::Ok(n);
}
void unCtrlC(){
	#ifdef _WIN32
	if(ctrlcEvent == INVALID_HANDLE_VALUE) return;
	#else
	#endif
	stahp = true;
	#ifdef _WIN32
	SetEvent(ctrlcEvent);
	ResourceHandle c = INVALID_HANDLE_VALUE;
	std::swap(ctrlcEvent, c);
	CloseHandle(c);
	//SetConsoleCtrlHandler(NULL, false);
	#else
	struct sigaction sigh = {};
	sigh.sa_handler = SIG_DFL;
	sigemptyset(&sigh.sa_mask);
	sigaction(SIGINT, &sigh, NULL);
	#endif
}

}
