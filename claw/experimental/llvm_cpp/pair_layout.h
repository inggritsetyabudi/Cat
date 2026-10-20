#pragma once
// Private LLVM implementation detail, never part of adapter.h or production ABI.
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <stdexcept>

namespace claw::experimental::llvm_cpp::detail {
inline void validatePairLayout(const llvm::DataLayout& layout, llvm::StructType* pair) {
    if (!pair || pair->isOpaque() || pair->isPacked() || pair->getNumElements() != 2 ||
        !pair->getElementType(0)->isIntegerTy(32) || !pair->getElementType(1)->isIntegerTy(32))
        throw std::runtime_error("LLVM C++ PoC unsupported pair layout shape");
    if (!layout.isLittleEndian() || layout.getPointerSizeInBits() != 64 ||
        layout.getPointerSizeInBits(270) != 32 || layout.getPointerSizeInBits(271) != 32 ||
        layout.getPointerSizeInBits(272) != 64 ||
        layout.getTypeSizeInBits(pair).getFixedValue() != 64 ||
        layout.getTypeAllocSize(pair).getFixedValue() != 8 ||
        layout.getTypeStoreSize(pair).getFixedValue() != 8 ||
        layout.getABITypeAlign(pair).value() != 4 ||
        layout.getPrefTypeAlign(pair).value() != 8 ||
        layout.getStructLayout(pair)->getElementOffset(0) != 0 ||
        layout.getStructLayout(pair)->getElementOffset(1) != 4)
        throw std::runtime_error("LLVM C++ PoC unsupported pair TargetMachine layout: size/alloc/store8 ABI-align4 preferred-align8 offsets0/4 little-endian pointer64 AS270/271/272=32/32/64 required");
}
} // namespace claw::experimental::llvm_cpp::detail
