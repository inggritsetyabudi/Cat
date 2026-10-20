#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace claw::frontend {

// An explicit, already-resolved CURRENT cleanup owner and a path relative to
// that owner. This is not historical allocation/RHS/move provenance. The key
// belongs to one semantic analysis; it is never reconstructed from a name.
struct SourceProjection {
    std::string bindingKey;
    std::vector<size_t> fields;

    bool isValid() const { return !bindingKey.empty(); }
};

// Inspection metadata only; existing bindingKey operands remain lookup keys.
// A nonzero synthetic ID identifies one materialized projection result within
// its containing OIR/LIR function, not a source binder or runtime allocation.
// IDs are not stable across functions, analyses, serialization or compilation.
// Unknown source is intentional (notably wildcard cleanup) and stays unknown.
struct Provenance {
    std::optional<SourceProjection> source;
    std::optional<uint64_t> syntheticStorageId;

    static Provenance forRoot(const std::string& bindingKey, uint64_t id) {
        Provenance result;
        if (!bindingKey.empty()) {
            result.source = SourceProjection{bindingKey, {}};
        }
        result.syntheticStorageId = id;
        return result;
    }

    Provenance project(size_t field, uint64_t id) const {
        Provenance result;
        if (source.has_value()) {
            result.source = *source;
            result.source->fields.push_back(field);
        }
        result.syntheticStorageId = id;
        return result;
    }

    Provenance withSyntheticStorageId(uint64_t id) const {
        Provenance result = *this;
        result.syntheticStorageId = id;
        return result;
    }

    // Structural validation only: this does not prove ownership, path bounds,
    // storage correspondence or a valid aggregate ABI. Consumers must not
    // interpret it as permission to lower an otherwise unsupported aggregate.
    bool isValid() const {
        return (!source.has_value() || source->isValid()) &&
            (!syntheticStorageId.has_value() || *syntheticStorageId != 0);
    }
};

inline bool isValidProvenance(const Provenance& provenance) {
    return provenance.isValid();
}

} // namespace claw::frontend
