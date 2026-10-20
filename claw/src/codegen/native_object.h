#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace claw::codegen {

// Transitional boundary: Cat still lowers LIR to its existing textual LLVM IR.
// LLVM parsing, verification, optimization and machine-code emission happen
// in this process. Runtime compilation and executable linking are separate.
// Returns the verified pre-optimization IR including the selected DataLayout.
std::string emitNativeObject(std::string_view nativeIr,
                             const std::filesystem::path& output,
                             bool optimize);

} // namespace claw::codegen
