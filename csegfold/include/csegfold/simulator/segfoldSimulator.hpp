#pragma once

#include "csegfold/simulator/simulator.hpp"

namespace csegfold {

class SegfoldSimulator : public Simulator {
public:
    SegfoldSimulator(const Matrix<int8_t>& A, const Matrix<int8_t>& B);
    
    void step();
    void cleanup_step();
    void run();
};

} // namespace csegfold

