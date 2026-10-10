#pragma once

#include "analysis/types.h"

#include <optional>
#include <string_view>

namespace claw::frontend::choice_utils {

std::optional<std::string> findChoiceVariantName(const ChoiceInfo& choice, std::string_view requested);
std::optional<ChoiceVariantInfo> resolveChoiceVariantInfo(
    const ChoiceInfo& choice,
    const ResolvedType& concreteType,
    std::string_view variantName);
bool isOutcomeLikeChoice(const ChoiceInfo& choice);

} // namespace claw::frontend::choice_utils
