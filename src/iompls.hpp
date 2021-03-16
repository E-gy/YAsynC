#pragma once

#include "io.hpp"

#include <utility>

namespace yasync::io {

/**
 * Creates a 2-way virtual IOI.
 * Everything written to one end can be read from the other and the other way around.
 * Primitive IO ops never fail.
 * Nothing stops the 2 ends from being operated by different engines.
 */
std::pair<IOResource, IOResource> ioi2Way(IOYengine*, IOYengine*);
/**
 * Creates a 2-way virtual IOI.
 * Everything written to one end can be read from the other and the other way around.
 * Primitive IO ops never fail.
 */
inline std::pair<IOResource, IOResource> ioi2Way(IOYengine* engine){ return ioi2Way(engine, engine); }

}
