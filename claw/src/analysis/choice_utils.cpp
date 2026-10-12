#include "analysis/choice_utils.h"

#include <string>

namespace claw::frontend::choice_utils {

std::optional<std::string> findChoiceVariantName(const ChoiceInfo& choice, std::string_view requested) {
    for (const auto& variantName : choice.variantOrder) {
        if (std::string_view(variantName) == requested) {
            return variantName;
        }
    }
    return std::nullopt;
}

std::optional<ChoiceVariantInfo> resolveChoiceVariantInfo(
    const ChoiceInfo& choice,
    const ResolvedType& concreteType,
    std::string_view variantName) {
    const auto matchedName = findChoiceVariantName(choice, variantName);
    if (!matchedName.has_value()) {
        return std::nullopt;
    }
    const auto bindings = buildTypeBindings(choice.typeParams, concreteType.params);
    const auto variantIt = choice.variants.find(*matchedName);
    if (variantIt == choice.variants.end()) {
        return std::nullopt;
    }

    ChoiceVariantInfo resolved;
    for (const auto& payloadType : variantIt->second.payloadTypes) {
        resolved.payloadTypes.push_back(substituteType(payloadType, bindings));
    }
    return resolved;
}

bool isOutcomeLikeChoice(const ChoiceInfo& choice) {
    const auto okName = findChoiceVariantName(choice, "Ok");
    const auto failName = findChoiceVariantName(choice, "Fail");
    if (!okName.has_value() || !failName.has_value() || choice.variantOrder.size() != 2) {
        return false;
    }
    const auto okIt = choice.variants.find(*okName);
    const auto failIt = choice.variants.find(*failName);
    if (okIt == choice.variants.end() || failIt == choice.variants.end()) {
        return false;
    }
    return okIt->second.payloadTypes.size() == 1 && failIt->second.payloadTypes.size() == 1;
}

} // namespace claw::frontend::choice_utils
