#pragma once

namespace csegfold {

class Simulator;
class MemoryController;
class SwitchModule;
class PEModule;

void run_b_loader(Simulator* simulator, MemoryController* controller, SwitchModule* switchModule);
void process_fifo_memory_responses(MemoryController* controller, SwitchModule* switchModule);
void run_memory_interface(MemoryController* controller, SwitchModule* switchModule, PEModule* peModule);

} // namespace csegfold

