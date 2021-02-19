#pragma once

#ifdef _DEBUG
#include <vector>
#endif

struct TraceCapture {
	#ifdef _DEBUG
	std::vector<void*> trace;
	#endif
	TraceCapture(unsigned upto = 32);
	static void setup();
};

#include <ostream>
std::ostream& operator<<(std::ostream& os, const TraceCapture& trace);
