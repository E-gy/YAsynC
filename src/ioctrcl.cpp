#include "ioctrlc.hpp"

#include "impls.hpp"
#include "io.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <semaphore.h>
#endif

namespace yasync::io {

void CtrlC::setup(){
	#ifdef _WIN32
	#else
	sigset_t sigs;
	::sigemptyset(&sigs);
	::sigaddset(&sigs, SIGINT);
	::pthread_sigmask(SIG_BLOCK, &sigs, NULL);
	#endif
}

#ifdef _WIN32
BOOL WINAPI glCtrlcHandler(DWORD sig){
#else
void glCtrlcHandler(int sig){
#endif
	return CtrlC_::INSTANCE()->ctrlcHandler(sig);
}

class CtrlC_ {
	bool stahp = false;
	#ifdef _WIN32
	ResourceHandle ctrlcEvent = INVALID_HANDLE_VALUE;
	#else
	::sem_t ctrlcEvent;
	#endif
	CtrlC_(){};
	public:
		static CtrlC_* INSTANCE(){
			static CtrlC_ instance;
			return &instance;
		}
		#ifdef _WIN32
		BOOL ctrlcHandler(DWORD sig){
			if(ctrlcEvent == INVALID_HANDLE_VALUE) return FALSE;
			switch(sig){
				case CTRL_C_EVENT:
				case CTRL_CLOSE_EVENT:
					::SetEvent(ctrlcEvent);
					return TRUE;
				default: return FALSE;
			}
		}
		#else
		void ctrlcHandler(int sig){
			if(sig == SIGINT) ::sem_post(&ctrlcEvent);
		}
		#endif
		result<Future<void>, std::string> on(Yengine* engine){
			stahp = false;
			#ifdef _WIN32
			if(ctrlcEvent != INVALID_HANDLE_VALUE) return result<Future<void>, std::string>::Err("ctrl+c handler already set!");
			ctrlcEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
			if(!::SetConsoleCtrlHandler(glCtrlcHandler, true)) return retSysError<result<Future<void>, std::string>>("Set ctrl+c handler failed");
			#else
			::sem_init(&ctrlcEvent, 0, 0);
			struct ::sigaction sa = {};
			sa.sa_handler = glCtrlcHandler;
			::sigemptyset(&sa.sa_mask);
			::sigaction(SIGINT, &sa, NULL);
			#endif
			std::shared_ptr<OutsideFuture<void>> n(new OutsideFuture<void>());
			Daemons::launch([=](){
				while(true){
					#ifdef _WIN32
					::WaitForSingleObject(ctrlcEvent, INFINITE);
					#else
					if(::sem_wait(&ctrlcEvent) < 0 && !stahp) continue;
					#endif
					if(stahp) break;
					n->s = FutureState::Completed;
					engine->notify(std::static_pointer_cast<IFutureT<void>>(n));
				}
				#ifdef _WIN32
				ResourceHandle c = INVALID_HANDLE_VALUE;
				std::swap(ctrlcEvent, c);
				::CloseHandle(c);
				//SetConsoleCtrlHandler(NULL, false);
				#else
				::sem_destroy(&ctrlcEvent);
				// struct sigaction sigh = {};
				// sigh.sa_handler = SIG_DFL;
				// ::sigemptyset(&sigh.sa_mask);
				// ::sigaction(SIGINT, &sigh, NULL);
				#endif
			});
			return result<Future<void>, std::string>::Ok(n);
		}
		void un(){
			#ifdef _WIN32
			if(ctrlcEvent == INVALID_HANDLE_VALUE) return;
			#else
			#endif
			stahp = true;
			#ifdef _WIN32
			::SetEvent(ctrlcEvent);
			#else
			::sem_post(&ctrlcEvent);
			#endif
		}
};

result<Future<void>, std::string> CtrlC::on(Yengine* engine){
	return CtrlC_::INSTANCE()->on(engine);
}
void CtrlC::un(){
	return CtrlC_::INSTANCE()->un();
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
