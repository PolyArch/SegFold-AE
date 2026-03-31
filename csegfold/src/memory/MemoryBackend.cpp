#include "csegfold/memory/MemoryBackend.hpp"
#include "csegfold/memory/IdealBackend.hpp"
#include "csegfold/memory/RamulatorBackend.hpp"
#include <iostream>

namespace csegfold {

std::unique_ptr<MemoryBackend> MemoryBackend::create(const MemoryBackendConfig& config) {
    std::unique_ptr<MemoryBackend> backend;

    if (config.type == "ideal") {
        backend = std::make_unique<IdealBackend>();
    } else if (config.type == "ramulator2") {
        backend = std::make_unique<RamulatorBackend>();
    } else {
        std::cerr << "Warning: Unknown memory backend type '" << config.type
                  << "', falling back to ideal backend." << std::endl;
        backend = std::make_unique<IdealBackend>();
    }

    backend->configure(config);
    return backend;
}

} // namespace csegfold
