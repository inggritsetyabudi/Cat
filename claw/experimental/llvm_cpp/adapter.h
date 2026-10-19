#pragma once

#include "ir/lir.h"
#include <string>

namespace claw::experimental::llvm_cpp {

struct ObjectOptions {
    std::string objectPath;
    std::string irPath;
    bool enableRuntimeI32 = false;
    // Existing native release/wrapping semantics only, not Cat debug safety.
    bool enableArithmeticI32 = false;
    // Bounded production String/Str ABI; requires existing Cat ownership lowering.
    bool enableStrings = false;
};

// Low-level opt-in PoC: scalar LIR and an explicitly enabled bounded String slice.
bool emitObject(const claw::frontend::LirProgram& program,
                const ObjectOptions& options,
                std::string& error);

} // namespace claw::experimental::llvm_cpp
