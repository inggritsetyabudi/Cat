#pragma once

#include "analysis/types.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace claw::frontend::builtin_methods {

struct BuiltinMethodSpec {
    std::string receiverName;
    std::string methodName;
    std::string receiverViewKind;
    std::vector<ResolvedType> paramTypes;
    ResolvedType returnType;
    std::optional<size_t> viewReturnSourceArg;
    bool viewReturnFromReceiver = false;
};

const std::vector<BuiltinMethodSpec>& builtinMethodSpecs();

} // namespace claw::frontend::builtin_methods
