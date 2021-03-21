#include "ticktack.hpp"

#include <optional>

namespace yasync {

TickTack::TickTack() : stahp(false) {
	worker = std::thread([this](){ threadwork(); });
}

void TickTack::shutdown(){
	std::set<El> rent;
	{
		std::unique_lock lok(lock);
		stahp = true;
		cvStahp.notify_one();
		std::swap(rent, ent);
	}
	for(auto t = rent.begin(); t != rent.end(); t++) t->f(t->id, true);
	worker.join();
}

TickTack::Id TickTack::start(const TimePoint& t, const Duration& d, Callback && f){
	std::unique_lock lok(lock);
	auto id = nid++;
	ent.emplace(TickTack::El{t, d, id, std::move(f)});
	return id;
}

void TickTack::stop(Id id){
	if(id) if(auto t = ([this, id]() -> std::optional<El> {
		std::unique_lock lok(lock);
		auto t = std::find_if(ent.begin(), ent.end(), [id](const El& el){ return el.id == id; });
		if(t == ent.end()) return std::nullopt;
		return std::move(ent.extract(t).value());
	})()) t->f(t->id, true);
}

void TickTack::threadwork(){
	auto next = [this]() -> std::optional<El> {
		if(stahp) return std::nullopt;
		std::unique_lock lok(lock);
		auto next = ent.begin();
		if(next == ent.end()) return std::nullopt;
		if(next->t > Clock::now()) return std::nullopt;
		return std::move(ent.extract(next).value());
	};
	while(!stahp){
		while(auto n = next()){
			auto tik = std::move(*n);
			tik.f(tik.id, false);
			if(tik.d != Duration::zero()){
				tik.t += tik.d;
				std::unique_lock lok(lock);
				ent.emplace(std::move(tik));
			}
		}
		if(!stahp){
			std::unique_lock lok(lock);
			cvStahp.wait_for(lok, std::chrono::milliseconds(5));
		}
	}
}

}
