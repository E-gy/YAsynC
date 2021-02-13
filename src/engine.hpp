#pragma once

#include <unordered_map>

#include <optional>
#include <memory>
#include <any>

#include "agen.hpp"
#include "future.hpp"
#include "threadsafequeue.hpp"

template<typename T> class FutureG : public Future<T> {
	public:
		std::shared_ptr<AGenerator<T>> gen;
		FutureState s = FutureState::Suspended;
		std::optional<T> val;
		FutureG(std::shared_ptr<AGenerator<T>> g) : gen(g) {}
		FutureState state(){ return s; }
		std::optional<T> result(){ return val; }
};

template<typename T> class IdentityGenerator : public AGenerator<T> {
	std::shared_ptr<Future<T>> w;
	bool reqd = false;
	public:
		IdentityGenerator(std::shared_ptr<Future<T>> awa) : w(awa) {}
		bool done() const { return reqd && w->state() == FutureState::Completed; }
		std::variant<std::shared_ptr<FutureBase>, T> resume([[maybe_unused]] const Yengine* engine){
			if(w->state() == FutureState::Completed) return *(w->result());
			if((reqd = !reqd)) return w;
			else return *(w->result());
		}
};

template<typename U, typename V, typename F> class ChainingGenerator : public AGenerator<V> {
	std::shared_ptr<Future<U>> w;
	bool reqd = false;
	F f;
	public:
		ChainingGenerator(std::shared_ptr<Future<U>> awa, F map) : w(awa), f(map) {}
		bool done() const { return reqd && w->state() == FutureState::Completed; }
		std::variant<std::shared_ptr<FutureBase>, V> resume([[maybe_unused]] const Yengine* engine){
			if(w->state() == FutureState::Completed) return f(*(w->result()));
			if((reqd = !reqd)) return w;
			else return f(*(w->result()));
		}
};

class Yengine {
	/**
	 * Queue<FutureG<?>>
	 */
	ThreadSafeQueue<std::shared_ptr<FutureBase>> work;
	/**
	 * Future → Future
	 */
	std::unordered_map<std::shared_ptr<FutureBase>, std::shared_ptr<FutureBase>> notifications;
	public:
		Yengine(unsigned threads);
		/**
		 * Transforms a generator into a future on this engine
		 * @param gen the generator
		 * @returns future
		 */ 
		template<typename T> std::shared_ptr<Future<T>> defer(std::shared_ptr<AGenerator<T>> gen){
			return std::shared_ptr<Future<T>>(new FutureG<T>(gen));
		}
		/**
		 * Resumes parallel yield of the future
		 * @param f future to execute
		 * @returns f
		 */ 
		template<typename T> std::shared_ptr<Future<T>> execute(std::shared_ptr<Future<T>> f){
			auto ft = std::dynamic_pointer_cast<FutureG<T>>(f);
			ft->s = FutureState::Queued;
			work.push(f);
			return f;
		}
		/**
		 * Transforms the generator into a future on this engine, and executes in parallel
		 * @param gen the generator
		 * @returns future
		 */ 
		template<typename T> std::shared_ptr<Future<T>> launch(std::shared_ptr<AGenerator<T>> gen){
			return execute(defer(gen));
		}
		/**
		 * Notifies the engine of completion of an external future.
		 * Returns almost immediately - actual processing of the notification will happen internally.
		 */
		template<typename T> void notify(std::shared_ptr<Future<T>> f){
			launch(std::shared_ptr<AGenerator<T>>(new IdentityGenerator<T>(f)));
		}
		/**
		 * @param f @ref future to map
		 * @returns @ref
		 */
		template<typename V, typename U, typename F> std::shared_ptr<Future<V>> then(std::shared_ptr<Future<U>> f, F map){
			return defer(std::shared_ptr<AGenerator<V>>(new ChainingGenerator<U, V, F>(f, map)));
		}
	private:
		std::mutex notificationsLock;
		void notifiAdd(std::shared_ptr<FutureBase> k, std::shared_ptr<FutureBase> v){
			std::unique_lock lock(notificationsLock);
			notifications[k] = v;
		}
		std::optional<std::shared_ptr<FutureBase>> notifiDrop(std::shared_ptr<FutureBase> k){
			std::unique_lock lock(notificationsLock);
			auto naut = notifications.find(k);
			if(naut == notifications.end()) return std::nullopt;
			notifications.erase(naut);
			return naut->second;
		}
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
						notifiAdd(*awa, task);
						task = *awa;
						// goto cont;
						break;
					case FutureState::Queued: //This is stoopid, but hey we don't want to sync what we don't need, so it'll wait
					case FutureState::Awaiting:
					case FutureState::Running:
						gent->s = FutureState::Awaiting;
						notifiAdd(*awa, task);
						return;
				}
			} else {
				auto v = std::get<std::any>(g);
				gent->s = gent->gen->done() ? FutureState::Completed : FutureState::Suspended;
				gent->val = std::optional(v);
				//#BeLazy: Whether we're done or not, drop from notifications. If we're done, well that's it. If we aren't, someone up in the pipeline will await for us at some point, setting up the notifications once again.
				if(auto naut = notifiDrop(task)) task = *naut; //Proceed up the await chain immediately
				else return;
			}
			}
		}
		void threadwork(){
			while(true){ //TODO !closed
				threado(work.pop());
			}
		}
};
