#pragma once

#include <unordered_map>

#include <optional>
#include <memory>
#include <any>

#include "agen.hpp"
#include "threadsafequeue.hpp"

enum class FutureState {
	Created, Running, Completed
};

/**
 * @interface
 */
class FutureBase {
	public:
		virtual FutureState state() = 0;
};

/**
 * @interface
 */
template<typename T> class Future : public FutureBase {
	public:
		virtual std::optional<T> result() = 0;
};

template<typename T> class FutureG : public Future<T> {
	public:
		std::shared_ptr<AGenerator<T>> gen;
		FutureG(std::shared_ptr<AGenerator<T>> g) : gen(g) {}
		FutureState s = FutureState::Created;
		FutureState state(){ return s; }
		std::optional<T> result(){ return gen->ret(); }
};

template<typename U, typename V, typename F> class ChainingGenerator : public AGenerator<V> {
	std::shared_ptr<Future<U>> w;
	F f;
	std::optional<V> result = std::nullopt;
	public:
		ChainingGenerator(Fstd::shared_ptr<Future<U>> awa, F map) : w(awa), f(map) {
			static_assert(std::is_function<F>);
		}
		bool done() const { return result.has_value(); }
		std::optional<V> ret() const { return result; }
		std::optional<std::shared_ptr<Future<U>>> resume(const Yengine* engine){
			if(done()) return std::nullopt;
			if(w->state() == FutureState::Completed){
				result = std::optional<V>(f(w->result()));
				return std::nullopt;
			}
			return w;
		}
};

class Yengine {
	/**
	 * Queue<FutureG<?>>
	 */
	ThreadSafeQueue<std::shared_ptr<FutureBase>> work;
	/**
	 * AGenerator<?> → Future
	 */
	//std::unordered_map<std::shared_ptr<std::any>, std::shared_ptr<FutureBase>> g2f;
	/**
	 * Future → Future
	 */
	std::unordered_map<std::shared_ptr<FutureBase>, std::shared_ptr<FutureBase>> notify; //TODO sync
	public:
		/**
		 * Resumes parallel yield of the generator
		 * @param gen @ref the generator to yield value from
		 * @returns @ref 
		 */ 
		template<typename T> std::shared_ptr<Future<T>> execute(std::shared_ptr<AGenerator<T>> gen){
			auto f = std::shared_ptr(new FutureG<T>(gen));
			work.push(f);
			return f;
		}
		/**
		 * @param f @ref future to map
		 * @returns @ref
		 */
		template<typename U, typename V, typename F> std::shared_ptr<Future<V>> then(std::shared_ptr<Future<U>> f, F map){
			return execute(std::shared_ptr(new ChainingGenerator<U, V, F>(f, map)));
		}
		void threado(std::shared_ptr<FutureBase> task){
			l0: while(task->state() == FutureState::Created){
				auto gent = std::dynamic_pointer_cast<FutureG<std::any>>(task);
				gent->s = FutureState::Running;
				l1: if(auto awa = gent->gen->resume(this)) switch(awa.value()->state()){
					case FutureState::Completed: //Current task awaits an already completed task. Resume immediately
						goto l1;
					case FutureState::Created: //Created but unstarted task. Execute it here and now; resume on notify.
						notify[awa.value()] = task; //Starting awa on this thread, even if it has been pushed to work queue, w/o clearing from work is fine -> it will be marked as running, and any thread that picks it up from the queue will skip it. FIXME SYNC! if another thread picks up the task at the same time this thread switches :(((
						task = awa.value();
						goto l0;
					case FutureState::Running: //Current task awaits awa, which is running in parallel. Resume on notify; go pick up more work.
						notify[awa.value()] = task;
						return;
				} else {
					auto naut = notify.find(task);
					auto na = naut != notify.end() ? std::optional(naut->second) : std::nullopt;
					if(gent->gen->done()){ //Current task is done, mark it completed and remove notify
						notify.erase(task);
						gent->s = FutureState::Completed;
					}
					if(na){
						if(gent->s == FutureState::Completed){
							task = na.value();
							goto l0;
						} else {
							//Ugh what a mess. So this is a fully fledged async generator. Currently the best thing to do is recurse, on the current thread right?
							//The big problem here is that _we_ don't capture return values.
							//And it's possible that the generator produces a new value, before the notify chain got time to capture the old one.
							//_even_ if we keep the generator running in the same thread
							//   it suffices for the next task to first sleep for a few seconds, _then_ capture preceding value.
							//which is a bad idea anyway
							//> Did i say this is a mess yet?
							//I guess we have to resort to `any` or some other heap container
							//Even if it incurs heap alloc performance penalties for primitive values :(
							//_The_ smart thing would be to keep primitives on stack, and indirect everything else
							//But that switch sounds very type-unsafe to me as it is...
							//FIXME
						}
					}
				}
				
			}
		}
		void threadwork(){
			while(true){ //TODO !closed
				threado(work.pop());
			}
		}
};
