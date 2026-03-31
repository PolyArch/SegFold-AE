#pragma once

namespace csegfold {

class Simulator;
class SwitchModule;
class PEModule;
class MemoryController;
class SPADModule;

void run_switches(Simulator* simulator, SwitchModule* switchModule, PEModule* peModule);
void analyze_idle_switches(Simulator* simulator, SwitchModule* switchModule);
void run_evictions(Simulator* simulator, MemoryController* controller,
                   SwitchModule* switchModule, PEModule* peModule);

} // namespace csegfold

