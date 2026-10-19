#pragma once

#include "ir/lir.h"
#include <string>

namespace claw::experimental::llvm_cpp {

struct ObjectOptions {
    std::string objectPath;
    std::string irPath;
};

// Low-level, opt-in PoC: lowers the supported scalar subset of LIR directly to LLVM.
bool emitObject(const claw::frontend::LirProgram& program,
                const ObjectOptions& options,
                std::string& error);

} // namespace claw::experimental::llvm_cpp
