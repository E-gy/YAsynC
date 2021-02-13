#pragma once

#include <unordered_map>

#include <optional>
#include <memory>
#include <any>

#include "agen.hpp"
#include "threadsafequeue.hpp"

enum class FutureState {
	Suspended, Running, Awaiting, Completed
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
		FutureState s = FutureState::Suspended;
		std::optional<T> val;
		FutureG(std::shared_ptr<AGenerator<T>> g) : gen(g) {}
		FutureState state(){ return s; }
		std::optional<T> result(){ return val; }
};

template<typename U, typename V, typename F> class ChainingGenerator : public AGenerator<V> {
	std::shared_ptr<Future<U>> w;
	bool reqd = false;
	F f;
	public:
		ChainingGenerator(Fstd::shared_ptr<Future<U>> awa, F map) : w(awa), f(map) {}
		bool done() const { return reqd && w->state() == FutureState::Completed; }
		std::variant<std::shared_ptr<Future<U>, V> resume(const Yengine* engine){
			if(w->state() == FutureState::Completed) return f(w->result());
			if((reqd = !reqd)) return w;
			else return f(w->result());
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
	std::unordered_map<std::shared_ptr<FutureBase>, std::shared_ptr<FutureBase>> notify; //TODO sync!!!
	public:
		/**
		 * Resumes parallel yield of the generator _immediately_
		 * @param gen @ref the generator to yield value from
		 * @returns @ref 
		 */ 
		template<typename T> std::shared_ptr<Future<T>> execute(std::shared_ptr<AGenerator<T>> gen){
			auto f = std::shared_ptr(new FutureG<T>(gen));
			work.push(f);
			return f;
		}
		/**
		 * Transforms a generator into lazy future, that will be evaluated once queried
		 * @param gen @ref the generator to yield value from
		 * @returns @ref 
		 */ 
		template<typename T> std::shared_ptr<Future<T>> defer(std::shared_ptr<AGenerator<T>> gen){
			return std::shared_ptr(new FutureG<T>(gen));
		}
		/**
		 * @param f @ref future to map
		 * @returns @ref
		 */
		template<typename U, typename V, typename F> std::shared_ptr<Future<V>> then(std::shared_ptr<Future<U>> f, F map){
			return defer(std::shared_ptr(new ChainingGenerator<U, V, F>(f, map)));
		}
	private:
		void threado(std::shared_ptr<FutureBase> task){
			if(task->state() != FutureState::Suspended) return; //Only suspended tasks are resumeable
			//cont:
			while(true)
			{
			auto gent = std::dynamic_pointer_cast<FutureG<std::any>>(task);
			gent->s = FutureState::Running;
			auto g = gent->gen->resume(this);
			if(auto awa = std::get_if<std::shared_ptr<FutureBase>>(&g)){
				switch((*awa)->state()){
					case FutureState::Completed:
						// goto cont;
						break;
					case FutureState::Suspended:
						gent->s = FutureState::Awaiting;
						notify[*awa] = task;
						task = *awa;
						// goto cont;
						break;
					case FutureState::Awaiting:
					case FutureState::Running:
						gent->s = FutureState::Awaiting;
						notify[*awa] = task;
						return;
				}
			} else {
				auto v = std::get<std::any>(g);
				gent->s = gent->gen->done() ? FutureState::Completed : FutureState::Suspended;
				gent->val = std::optional(v);
				auto naut = notify.find(task);
				if(naut != notify.end()){
					notify.erase(naut); //#BeLazy: Whether we're done or not, drop from notifications. If we're done, well that's it. If we aren't, someone up in the pipeline will await for us at some point, setting up the notifications once again.
					task = naut->second; //Proceed up the await chain immediately
					// goto cont;
					break;
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
