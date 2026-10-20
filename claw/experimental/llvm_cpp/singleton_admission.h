#pragma once

#include "ir/lir.h"

namespace claw::experimental::llvm_cpp {

// Fail-closed admission for the opt-in Tuple[Int32] backend subset.
// This is deliberately a structural validator, not an aggregate lowerer.
void validateSingletonAdmission(const claw::frontend::LirProgram& program);

} // namespace claw::experimental::llvm_cpp
