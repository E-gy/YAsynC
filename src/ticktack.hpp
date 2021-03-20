#pragma once

#include <mutex>
#include <condition_variable>
#include <functional>
#include <set>
#include <chrono>
#include <atomic>

namespace yasync {

/**
 * Tuck Tack Teck the time has come!
 * Timed operations with ~10ms precision.
 */
class TickTack {
	public:
		using Id = unsigned long long;
		const Id UnId = 0;
		using Clock = std::chrono::steady_clock;
		using TimePoint = std::chrono::time_point<Clock>;
		using Duration = Clock::duration;
		/**
		 * Callback to be invoked when time comes.
		 * Accepts timer and whether it's a scheduled call or cancellation.
		 * The callback should make zero assumptions about thread it will be invoked in.
		 * @param tid timer id
		 * @param cancell is cancellation
		 */
		using Callback = std::function<void(Id, bool)>;
	private:
		struct El {
			TimePoint t;
			Duration d;
			Id id;
			Callback f;
			inline El(const TimePoint& nt, const Duration& i, Id tid, Callback && cb) noexcept : t(nt), d(i), id(tid), f(std::move(cb)) {}
			inline El(El && mov) noexcept : t(std::move(mov.t)), d(std::move(mov.d)), id(std::move(mov.id)), f(std::move(mov.f)) {}
			inline El& operator=(El && mov) noexcept {
				t = std::move(mov.t);
				id = std::move(mov.id);
				f = std::move(mov.f);
				d = std::move(mov.d);
				return *this;
			}
			El(const El&) = delete;
			El& operator=(const El&) = delete;
			inline bool oneoff() const noexcept { return d == Duration::zero(); }
			inline bool operator<(const El& other) const noexcept { return t == other.t ? id < other.id : t < other.t; }
		};
		std::mutex lock;
		std::set<El> ent;
		Id nid = UnId+1;
	public:
		TickTack();
		/**
		 * Shutdown stops all active timers
		 */
		void shutdown();
		/**
		 * Starts a timer (general impl point)
		 * @param t next time to come
		 * @param d interval after that, or `0` for one-off
		 * @param f timer callback
		 */
		Id start(const TimePoint& t, const Duration& d, Callback && f);
		inline Id after(const TimePoint& t, Callback && f){ return start(t, Duration::zero(), std::move(f)); }
		inline Id after(const Duration& t, Callback && f){ return after(Clock::now() + t, std::move(f)); }
		inline Id sleep(const Duration& t, Callback && f){ return after(t, std::move(f)); }
		inline Id timer(const Duration& t, Callback && f){ return start(Clock::now() + t, t, std::move(f)); }
		/**
		 * Stops the timer
		 */
		void stop(Id);
	private:
		std::shared_ptr<std::atomic_bool> stahp;
		std::condition_variable cvStahp;
		void threadwork();
};

}
