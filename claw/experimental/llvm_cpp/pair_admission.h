#pragma once

#include "ir/lir.h"

namespace claw::experimental::llvm_cpp {

// Fail-closed admission for the opt-in Tuple[Int32, Int32] backend subset.
// Pair construction is tracked structurally; this is not an aggregate lowerer.
void validatePairAdmission(const claw::frontend::LirProgram& program,
                           bool enableArithmeticI32);

} // namespace claw::experimental::llvm_cpp
