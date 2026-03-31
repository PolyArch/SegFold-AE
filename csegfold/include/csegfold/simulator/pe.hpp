#pragma once

#include "csegfold/modules/pe.hpp"

namespace csegfold {

class Simulator;
class PEModule;
class SPADModule;

void to_mac(PEModule* peModule, int i, int j);
void run_pes(Simulator* simulator, PEModule* peModule);

// C value transfer functions
bool store_c_from_pe_to_spad(SPADModule* spadModule, PEModule* peModule, int i, int j);
bool load_c_from_spad_to_pe(SPADModule* spadModule, PEModule* peModule, int i, int j);

} // namespace csegfold

