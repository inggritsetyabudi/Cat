#include "analysis/builtin_methods.h"

namespace claw::frontend::builtin_methods {
namespace {

ResolvedType makePlainType(const std::string& name) {
    ResolvedType type;
    type.name = name;
    type.category = TypeCategory::Plain;
    return type;
}

ResolvedType makeOwnedType(const std::string& name) {
    ResolvedType type;
    type.name = name;
    type.category = TypeCategory::Owned;
    return type;
}

ResolvedType asViewType(const ResolvedType& base, const std::string& viewKind) {
    ResolvedType adapted = base;
    adapted.viewKind = viewKind;
    adapted.viewScope = base.viewScope;
    adapted.category = viewKind.empty() ? base.category : TypeCategory::View;
    return adapted;
}

} // namespace

const std::vector<BuiltinMethodSpec>& builtinMethodSpecs() {
    static const std::vector<BuiltinMethodSpec> specs = [] {
        std::vector<BuiltinMethodSpec> entries;
        auto addSizedLookMethods = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "len", "look", {}, makePlainType("USize")});
            entries.push_back(BuiltinMethodSpec{receiverName, "is_empty", "look", {}, makePlainType("Bool")});
        };
        auto addByteIndexMethod = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{
                receiverName,
                "byte_at",
                "look",
                {makePlainType("USize")},
                makePlainType("UInt8")});
        };
        auto addByteEdgeMethods = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "first_byte", "look", {}, makePlainType("UInt8")});
            entries.push_back(BuiltinMethodSpec{receiverName, "last_byte", "look", {}, makePlainType("UInt8")});
            entries.push_back(BuiltinMethodSpec{receiverName, "find_byte", "look", {makePlainType("UInt8")}, makePlainType("Int64")});
            entries.push_back(BuiltinMethodSpec{receiverName, "count_byte", "look", {makePlainType("UInt8")}, makePlainType("USize")});
        };
        auto addTextSearchMethods = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "starts_with", "look", {asViewType(makeOwnedType(receiverName), "look")}, makePlainType("Bool")});
            entries.push_back(BuiltinMethodSpec{receiverName, "ends_with", "look", {asViewType(makeOwnedType(receiverName), "look")}, makePlainType("Bool")});
        };
        auto addCapacityLookMethod = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "capacity", "look", {}, makePlainType("USize")});
            entries.push_back(BuiltinMethodSpec{receiverName, "has_capacity", "look", {makePlainType("USize")}, makePlainType("Bool")});
        };
        auto addClearEditMethod = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "clear", "edit", {}, makePlainType("Unit")});
        };
        auto addReserveEditMethod = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "reserve", "edit", {makePlainType("USize")}, makePlainType("Unit")});
        };
        auto addTruncateEditMethod = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "truncate", "edit", {makePlainType("USize")}, makePlainType("Unit")});
        };
        auto addShrinkToFitEditMethod = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "shrink_to_fit", "edit", {}, makePlainType("Unit")});
        };
        auto addSliceViewMethod = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{
                receiverName,
                "slice",
                "look",
                {makePlainType("USize"), makePlainType("USize")},
                asViewType(makeOwnedType(receiverName), "look"),
                std::nullopt,
                true});
        };

        addSizedLookMethods("Str");
        addSizedLookMethods("Span");
        addSizedLookMethods("Vec");
        addSizedLookMethods("Map");
        addSizedLookMethods("Set");
        addSizedLookMethods("Queue");

        addByteIndexMethod("Str");
        addByteEdgeMethods("Str");
        addTextSearchMethods("Str");

        entries.push_back(BuiltinMethodSpec{"Str", "contains", "look", {asViewType(makeOwnedType("Str"), "look")}, makePlainType("Bool")});
        entries.push_back(BuiltinMethodSpec{"Str", "contains_byte", "look", {makePlainType("UInt8")}, makePlainType("Bool")});

        addSliceViewMethod("Str");
        addSliceViewMethod("Span");

        addCapacityLookMethod("Vec");
        addCapacityLookMethod("Map");
        addCapacityLookMethod("Set");
        addCapacityLookMethod("Queue");

        addClearEditMethod("Vec");
        addClearEditMethod("Map");
        addClearEditMethod("Set");
        addClearEditMethod("Queue");

        addReserveEditMethod("Vec");
        addReserveEditMethod("Map");
        addReserveEditMethod("Set");
        addReserveEditMethod("Queue");

        addTruncateEditMethod("Vec");
        addTruncateEditMethod("Queue");

        addShrinkToFitEditMethod("Vec");
        addShrinkToFitEditMethod("Map");
        addShrinkToFitEditMethod("Set");
        addShrinkToFitEditMethod("Queue");
        return entries;
    }();
    return specs;
}

} // namespace claw::frontend::builtin_methods
