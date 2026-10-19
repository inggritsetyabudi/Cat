#include "codegen/llvm_internal.h"

namespace claw::codegen {

std::string_view nativeTargetTriple() noexcept {
#if defined(__APPLE__)
#if defined(__aarch64__)
    return "aarch64-apple-darwin";
#else
    return "x86_64-apple-darwin";
#endif
#elif defined(_WIN32)
    return "x86_64-w64-windows-gnu";
#elif defined(__linux__)
#if defined(__aarch64__)
    return "aarch64-unknown-linux-gnu";
#else
    return "x86_64-unknown-linux-gnu";
#endif
#else
    return "x86_64-unknown-unknown";
#endif
}

std::string emitLlvmIr(const LirProgram& program) {
    return LlvmEmitter(program).emit();
}

std::string emitLlvmIr(std::string_view entryRealm, const std::vector<OirUnitView>& units) {
    return emitLlvmIr(buildLirProgram(entryRealm, units));
}

std::string emitNativeLlvmIr(const LirProgram& program) {
    return LlvmEmitter(program, true).emit();
}

std::string emitNativeLlvmIr(std::string_view entryRealm, const std::vector<OirUnitView>& units) {
    return emitNativeLlvmIr(buildLirProgram(entryRealm, units));
}

} // namespace claw::codegen
