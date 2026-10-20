#include "experimental/llvm_cpp/adapter.h"
#include "experimental/llvm_cpp/singleton_admission.h"
#include "experimental/llvm_cpp/pair_admission.h"
#include "experimental/llvm_cpp/pair_layout.h"
#include "codegen/llvm.h"
#include "codegen/llvm_internal.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>

#include <charconv>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#if defined(_WIN32) && defined(__MINGW64__)
#include <_mingw.h>
#endif
#if defined(CLAW_LLVM_CPP_POC_UCRT_X64) && (!defined(_WIN32) || !defined(__MINGW64__) || !defined(_UCRT))
#error "Windows PoC objects require the native MinGW UCRT toolchain"
#endif

#if LLVM_VERSION_MAJOR != 23 || LLVM_VERSION_MINOR != 1 || LLVM_VERSION_PATCH != 3
#error "The experimental Cat adapter requires exact LLVM 23.1.3 headers"
#endif

namespace claw::experimental::llvm_cpp {
namespace {
using namespace claw::frontend;

[[noreturn]] void unsupported(const std::string& detail) {
    throw std::runtime_error("LLVM C++ PoC unsupported " + detail);
}
void rejectProjectionProvenance(const Provenance& provenance) {
    if (!provenance.isValid()) unsupported("invalid cleanup provenance metadata");
    // This is an inspection/diagnostic boundary, not aggregate storage support.
    // Never use a source binding or a synthetic storage ID as a scalar key.
    if (provenance.source || provenance.syntheticStorageId)
        unsupported("tuple cleanup provenance (aggregate lowering remains unsupported)");
}
llvm::Type* scalarType(llvm::LLVMContext& context, const std::string& name) {
    // Deliberately do not erase ref/view prefixes or reinterpret aggregate ABI.
    if (name == "Int32") return llvm::Type::getInt32Ty(context);
    if (name == "Bool") return llvm::Type::getInt1Ty(context);
    if (name == "Unit") return llvm::Type::getVoidTy(context);
    unsupported("scalar type '" + name + "'");
}

bool isSliceType(const std::string& name) { return name == "Str" || name == "ref Str"; }
bool isBufferType(const std::string& name) { return name == "String"; }
bool isStringType(const std::string& name) { return isSliceType(name) || isBufferType(name); }

bool isSingletonType(const std::string& name) { return name == "Tuple[Int32]"; }
bool isPairType(const std::string& name) { return name == "Tuple[Int32, Int32]"; }
llvm::Type* stringType(llvm::LLVMContext& context, const std::string& name, bool enabled, bool singleton = false, bool pair = false) {
    if (pair && isPairType(name))
        return llvm::StructType::get(context, {llvm::Type::getInt32Ty(context), llvm::Type::getInt32Ty(context)}, false);
    if (singleton && isSingletonType(name))
        return llvm::StructType::get(context, {llvm::Type::getInt32Ty(context)}, false);
    if (!enabled || !isStringType(name)) return scalarType(context, name);
    const char* tag = isSliceType(name) ? "claw.slice" : "claw.buffer";
    if (auto* existing = llvm::StructType::getTypeByName(context, tag)) return existing;
    auto* record = llvm::StructType::create(context, tag);
    std::vector<llvm::Type*> fields{llvm::PointerType::getUnqual(context), llvm::Type::getInt64Ty(context)};
    if (isBufferType(name)) fields.push_back(llvm::Type::getInt64Ty(context));
    record->setBody(fields, false);
    return record;
}

bool isBuiltinTemplate(const LirChoice& choice) {
    if (!choice.layout || !choice.layout->isTemplate || choice.cases.size() != 2) return false;
    if (choice.name == "Result" && choice.typeParams == std::vector<std::string>{"T", "E"}) {
        return choice.cases[0].name == "Ok" && choice.cases[0].payloadTypes == std::vector<std::string>{"T"} &&
               choice.cases[1].name == "Fail" && choice.cases[1].payloadTypes == std::vector<std::string>{"E"};
    }
    if (choice.name == "Maybe" && choice.typeParams == std::vector<std::string>{"T"}) {
        return choice.cases[0].name == "None" && choice.cases[0].payloadTypes.empty() &&
               choice.cases[1].name == "Some" && choice.cases[1].payloadTypes == std::vector<std::string>{"T"};
    }
    return false;
}

struct FunctionInfo {
    const LirFunction* lir;
    llvm::Function* ir;
};
using FunctionMap = std::unordered_map<std::string, FunctionInfo>;

class FunctionLowerer {
public:
    FunctionLowerer(llvm::LLVMContext& context, const FunctionInfo& function,
                    const FunctionMap& functions, llvm::Function* entry, llvm::Function* runtimeI32,
                    llvm::Function* runtimeDivisionByZero, bool enableArithmeticI32,
                    bool enableStrings, bool enableSingletonI32, bool enablePairI32, llvm::Module* module,
                    llvm::Function* runtimeStringAlloc, llvm::Function* runtimeStringCopy,
                    llvm::Function* runtimeStringFree, llvm::Function* runtimePrintSlice)
        : context_(context), builder_(context), function_(function), functions_(functions), entry_(entry),
          runtimeI32_(runtimeI32), runtimeDivisionByZero_(runtimeDivisionByZero),
          enableArithmeticI32_(enableArithmeticI32), enableStrings_(enableStrings),
          enableSingletonI32_(enableSingletonI32), enablePairI32_(enablePairI32), module_(module),
          runtimeStringAlloc_(runtimeStringAlloc), runtimeStringCopy_(runtimeStringCopy),
          runtimeStringFree_(runtimeStringFree), runtimePrintSlice_(runtimePrintSlice) {}

    void lower() {
        for (const auto& block : function_.lir->blocks) {
            if (!blocks_.emplace(block.label, llvm::BasicBlock::Create(context_, block.label, function_.ir)).second)
                unsupported("duplicate block label '" + block.label + "'");
        }
        if (blocks_.empty()) unsupported("function without blocks");
        size_t argumentIndex = function_.lir->returnType == "String" ? 1 : 0;
        std::unordered_set<std::string> parameterNames;
        std::unordered_set<std::string> parameterKeys;
        for (size_t i = 0; i < function_.lir->params.size(); ++i, ++argumentIndex) {
            const auto& param = function_.lir->params[i];
            const auto& name = param.name;
            if (!parameterNames.emplace(name).second)
                unsupported("duplicate parameter name '" + name + "'");
            const std::string id = param.bindingKey.empty() ? name : param.bindingKey;
            if (!parameterKeys.emplace(id).second)
                unsupported("duplicate parameter binding key '" + id + "'");
            auto* argument = function_.ir->getArg(argumentIndex);
            argument->setName(name);
            if (argument->getType()->isPointerTy() && isStringType(param.type)) {
                if (!parameterAddresses_.emplace(id, argument).second)
                    unsupported("duplicate parameter slot '" + id + "'");
                parameterTypes_.emplace(id, stringType(context_, param.type, enableStrings_, enableSingletonI32_, enablePairI32_));
            } else if (!values_.emplace(id, argument).second) {
                unsupported("duplicate parameter value '" + id + "'");
            }
        }
        for (const auto& block : function_.lir->blocks) {
            builder_.SetInsertPoint(blocks_.at(block.label));
            for (const auto& instruction : block.insts) {
                if (builder_.GetInsertBlock()->getTerminatorOrNull()) unsupported("instruction after terminator");
                std::visit([this](const auto& inst) { lowerInstruction(inst); }, instruction);
            }
            if (!builder_.GetInsertBlock()->getTerminatorOrNull()) {
                if (function_.lir->returnType != "Unit") unsupported("unterminated non-Unit block '" + block.label + "'");
                builder_.CreateRetVoid();
            }
        }
    }

private:
    static std::string key(const LirValue& value) {
        return value.bindingKey.empty() ? value.text : value.bindingKey;
    }
    llvm::BasicBlock* block(const std::string& label) const {
        const auto found = blocks_.find(label);
        if (found == blocks_.end()) unsupported("unknown block label '" + label + "'");
        return found->second;
    }
    llvm::AllocaInst* allocate(llvm::Type* type, const std::string& name) {
        // Every spill has one static slot per call site, not one allocation per
        // loop iteration. Initialization remains at the original use site.
        llvm::IRBuilder<> storage(&function_.ir->getEntryBlock(), function_.ir->getEntryBlock().begin());
        auto* slot = storage.CreateAlloca(type, nullptr, name);
        if (enableSingletonI32_ || enablePairI32_)
            slot->setAlignment(module_->getDataLayout().getABITypeAlign(type));
        return slot;
    }
    llvm::Value* value(const LirValue& input) {
        rejectProjectionProvenance(input.provenance);
        auto* expectedType = stringType(context_, input.type, enableStrings_, enableSingletonI32_, enablePairI32_);
        if (input.isUnit || expectedType->isVoidTy()) unsupported("Unit as a scalar operand");
        llvm::Value* result = nullptr;
        const auto id = key(input);
        if (const auto found = slots_.find(id); found != slots_.end()) {
            result = builder_.CreateLoad(found->second->getAllocatedType(), found->second, input.text + ".load");
        } else if (const auto found = parameterAddresses_.find(id); found != parameterAddresses_.end()) {
            if (parameterTypes_.at(id) != expectedType) unsupported("indirect parameter type mismatch");
            result = builder_.CreateLoad(expectedType, found->second, input.text + ".arg.load");
        } else if (const auto found = values_.find(id); found != values_.end()) {
            result = found->second;
        } else if (enableStrings_ && input.bindingKey.empty() && isSliceType(input.type) && claw::codegen::isStringLiteral(input.text)) {
            const std::string decoded = claw::codegen::decodeStringLiteral(input.text);
            const std::string name = ".claw.str." + std::to_string(literalId_++);
            auto* array = llvm::ConstantDataArray::getString(context_, decoded, true);
            auto* global = new llvm::GlobalVariable(*module_, array->getType(), true,
                llvm::GlobalValue::PrivateLinkage, array, name);
            llvm::SmallVector<llvm::Constant*, 2> indices{
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0),
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0)};
            auto* ptr = llvm::ConstantExpr::getInBoundsGetElementPtr(array->getType(), global, indices);
            result = llvm::ConstantStruct::get(llvm::cast<llvm::StructType>(expectedType),
                {ptr, llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), decoded.size())});
        } else if (!input.bindingKey.empty()) {
            unsupported("unresolved binding key '" + input.bindingKey + "'");
        } else if (input.type == "Bool" && (input.text == "true" || input.text == "false")) {
            result = llvm::ConstantInt::get(expectedType, input.text == "true");
        } else if (input.type == "Int32") {
            int32_t number = 0;
            const auto [end, error] = std::from_chars(input.text.data(), input.text.data() + input.text.size(), number);
            if (error == std::errc{} && end == input.text.data() + input.text.size())
                result = llvm::ConstantInt::getSigned(expectedType, number);
        }
        if (!result) unsupported("unresolved scalar value '" + input.text + "' (binding '" + input.bindingKey + "')");
        if (result->getType() != expectedType) unsupported("LIR operand type mismatch for '" + input.text + "'");
        return result;
    }
    void bind(const std::string& name, llvm::Value* result) {
        if (!result || result->getType()->isVoidTy()) unsupported("Unit result binding");
        if (!values_.emplace(name, result).second || slots_.contains(name) || parameterAddresses_.contains(name)) unsupported("duplicate SSA result '" + name + "'");
    }
    void lowerInstruction(const LirObjectInst& inst) {
        auto* type = stringType(context_, inst.type, enableStrings_, enableSingletonI32_, enablePairI32_);
        if (type->isVoidTy()) unsupported("Unit local storage");
        const auto id = inst.bindingKey.empty() ? inst.name : inst.bindingKey;
        if (slots_.contains(id) || values_.contains(id) || parameterAddresses_.contains(id)) unsupported("duplicate object binding '" + id + "'");
        if (inst.storage != "stack") unsupported("object storage '" + inst.storage + "'");
        // Hoist storage, not initialization, so nested/loop-local allocas dominate uses.
        slots_.emplace(id, allocate(type, inst.name + ".slot"));
    }
    void lowerInstruction(const LirStoreInst& inst) {
        const auto id = inst.targetBindingKey.empty() ? inst.target : inst.targetBindingKey;
        const auto found = slots_.find(id);
        if (found == slots_.end()) unsupported("store target without local storage '" + inst.target + "'");
        auto* operand = value(inst.value);
        if (operand->getType() != found->second->getAllocatedType()) unsupported("store type mismatch");
        builder_.CreateStore(operand, found->second);
    }
    void lowerInstruction(const LirReturnInst& inst) {
        if (function_.ir->getReturnType()->isVoidTy()) {
            if (function_.lir->returnType == "String") {
                if (inst.value.type != "String") unsupported("String return type mismatch");
                auto* slot = function_.ir->getArg(0);
                builder_.CreateStore(value(inst.value), slot);
                builder_.CreateRetVoid();
            } else {
                if (!inst.value.isUnit && inst.value.type != "Unit") unsupported("value return in Unit function");
                builder_.CreateRetVoid();
            }
        } else {
            auto* operand = value(inst.value);
            if (operand->getType() != function_.ir->getReturnType()) unsupported("return type mismatch");
            builder_.CreateRet(operand);
        }
    }
    void lowerInstruction(const LirBranchInst& inst) {
        auto* condition = value(inst.condition);
        if (!condition->getType()->isIntegerTy(1)) unsupported("non-Bool branch condition");
        builder_.CreateCondBr(condition, block(inst.trueLabel), block(inst.falseLabel));
    }
    void lowerInstruction(const LirGotoInst& inst) { builder_.CreateBr(block(inst.targetLabel)); }
    void lowerInstruction(const LirBreakInst& inst) {
        if (!enableStrings_) unsupported("LIR break outside String control slice");
        builder_.CreateBr(block(inst.targetLabel));
    }
    void lowerInstruction(const LirContinueInst& inst) {
        if (!enableStrings_) unsupported("LIR continue outside String control slice");
        builder_.CreateBr(block(inst.targetLabel));
    }
    void lowerInstruction(const LirPhiInst& inst) {
        // Current Cat LIR uses Control phi as metadata. Value phis are not covered.
        if (inst.type != "Control") unsupported("value phi instruction");
    }
    void lowerStringDrop(const LirDropInst& inst) {
        if (!enableStrings_ || inst.type != "String" || !runtimeStringFree_)
            unsupported("malformed String drop");
        auto* owner = value(LirValue{inst.name, inst.type, false, inst.bindingKey});
        builder_.CreateCall(runtimeStringFree_, {builder_.CreateExtractValue(owner, {0}, "string.drop.data")});
    }
    void lowerInstruction(const LirCallInst& inst) {
        if (enableStrings_ && inst.kind == LirCallKind::Runtime && inst.callee == "println" &&
            inst.hook == "runtime.println" && inst.type == "Unit" && inst.args.size() == 1 &&
            isSliceType(inst.args.front().type) && !inst.result && !inst.builtinTag && !inst.builtinReceiver &&
            !inst.externalInfo && inst.safety == LirSafetyTag::ProvenSafe) {
            auto* argument = value(inst.args.front());
            auto* slot = allocate(argument->getType(), "println.slice.addr");
            builder_.CreateStore(argument, slot);
            builder_.CreateCall(runtimePrintSlice_, {slot});
            return;
        }
        if (enableStrings_ && inst.kind == LirCallKind::Builtin && inst.safety == LirSafetyTag::ProvenSafe &&
            inst.builtinTag.has_value()) {
            if (inst.externalInfo || inst.hook != "builtin." + *inst.builtinTag)
                unsupported("malformed String builtin metadata");
            if (*inst.builtinTag == "String.from") {
                if (inst.args.size() != 1 || !isSliceType(inst.args[0].type) || inst.type != "String" ||
                    inst.builtinReceiver || !inst.result || !runtimeStringAlloc_ || !runtimeStringCopy_)
                    unsupported("malformed String.from");
                auto* slice = value(inst.args[0]);
                auto* ptr = builder_.CreateExtractValue(slice, {0}, "string.from.ptr");
                auto* len = builder_.CreateExtractValue(slice, {1}, "string.from.len");
                auto* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 0);
                auto* one = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), 1);
                auto* cap = builder_.CreateSelect(builder_.CreateICmpSGT(len, zero), len, one, "string.from.capacity");
                auto* data = builder_.CreateCall(runtimeStringAlloc_, {cap}, "string.from.data");
                builder_.CreateCall(runtimeStringCopy_, {data, ptr, len});
                llvm::Value* result = llvm::ConstantStruct::getNullValue(stringType(context_, "String", true, false, enablePairI32_));
                result = builder_.CreateInsertValue(result, data, {0}, "string.from.ptr.field");
                result = builder_.CreateInsertValue(result, len, {1}, "string.from.len.field");
                result = builder_.CreateInsertValue(result, cap, {2}, inst.result.value_or("string.from.capacity.field"));
                if (inst.result) bind(*inst.result, result);
                return;
            }
            if (*inst.builtinTag == "String.as_str" || *inst.builtinTag == "String.clone") {
                if (!inst.builtinReceiver || !inst.args.empty() || inst.builtinReceiver->type != "String")
                    unsupported("malformed String receiver builtin");
                auto* owner = value(*inst.builtinReceiver);
                if (*inst.builtinTag == "String.as_str") {
                    if (inst.type != "ref Str") unsupported("as_str result type");
                    llvm::Value* result = llvm::ConstantStruct::getNullValue(stringType(context_, "ref Str", true, false, enablePairI32_));
                    result = builder_.CreateInsertValue(result, builder_.CreateExtractValue(owner, {0}), {0}, "string.as_str.ptr");
                    result = builder_.CreateInsertValue(result, builder_.CreateExtractValue(owner, {1}), {1}, inst.result.value_or("string.as_str.len"));
                    if (inst.result) bind(*inst.result, result);
                    return;
                }
                if (inst.type != "String" || !inst.result) unsupported("clone result type/binding");
                auto* data = builder_.CreateCall(runtimeStringAlloc_, {builder_.CreateExtractValue(owner, {2})}, "string.clone.data");
                builder_.CreateCall(runtimeStringCopy_, {data, builder_.CreateExtractValue(owner, {0}), builder_.CreateExtractValue(owner, {1})});
                llvm::Value* result = llvm::ConstantStruct::getNullValue(stringType(context_, "String", true, false, enablePairI32_));
                result = builder_.CreateInsertValue(result, data, {0}, "string.clone.ptr");
                result = builder_.CreateInsertValue(result, builder_.CreateExtractValue(owner, {1}), {1}, "string.clone.len");
                result = builder_.CreateInsertValue(result, builder_.CreateExtractValue(owner, {2}), {2}, inst.result.value_or("string.clone.cap"));
                if (inst.result) bind(*inst.result, result);
                return;
            }
            unsupported("unsupported String builtin '" + *inst.builtinTag + "'");
        }
        if (inst.kind == LirCallKind::Runtime) {
            if (!runtimeI32_ || inst.callee != "println" || inst.hook != "runtime.println" ||
                inst.type != "Unit" || inst.result || inst.externalInfo || inst.builtinTag ||
                inst.builtinReceiver || inst.safety != LirSafetyTag::ProvenSafe ||
                inst.args.size() != 1 || inst.args.front().type != "Int32")
                unsupported("unsupported runtime call '" + inst.callee + "'");
            auto* argument = value(inst.args.front());
            if (!argument->getType()->isIntegerTy(32)) unsupported("runtime println Int32 ABI mismatch");
            builder_.CreateCall(runtimeI32_, {argument});
            return;
        }
        if ((inst.kind != LirCallKind::Direct && inst.kind != LirCallKind::ModuleDirect) ||
            inst.externalInfo || inst.builtinTag || inst.builtinReceiver)
            unsupported("non-direct call '" + inst.callee + "'");
        const auto found = functions_.find(inst.callee);
        if (found == functions_.end()) unsupported("unknown call target '" + inst.callee + "'");
        auto* callee = found->second.ir;
        if (callee == entry_) unsupported("call to entry main");
        if (callee == function_.ir) unsupported("recursive direct call");
        if (stringType(context_, inst.type, enableStrings_, enableSingletonI32_, enablePairI32_) != stringType(context_, found->second.lir->returnType, enableStrings_, enableSingletonI32_, enablePairI32_))
            unsupported("call logical result type mismatch");
        const bool indirectReturn = found->second.lir->returnType == "String";
        if (inst.args.size() + (indirectReturn ? 1 : 0) != callee->arg_size()) unsupported("call argument count mismatch");
        if ((!indirectReturn && stringType(context_, inst.type, enableStrings_, enableSingletonI32_, enablePairI32_) != callee->getReturnType()) ||
            (indirectReturn && !callee->getReturnType()->isVoidTy())) unsupported("call result type mismatch");
        std::vector<llvm::Value*> arguments;
        llvm::AllocaInst* returnSlot = nullptr;
        if (indirectReturn) {
            if (!inst.result) unsupported("unbound owned call result");
            returnSlot = allocate(stringType(context_, "String", true, false, enablePairI32_), "call.ret.addr");
            arguments.push_back(returnSlot);
        }
        for (size_t i = 0; i < inst.args.size(); ++i) {
            if (stringType(context_, inst.args[i].type, enableStrings_, enableSingletonI32_, enablePairI32_) !=
                stringType(context_, found->second.lir->params[i].type, enableStrings_, enableSingletonI32_, enablePairI32_))
                unsupported("call logical argument type mismatch");
            auto* operand = value(inst.args[i]);
            auto* expected = callee->getFunctionType()->getParamType(i + (indirectReturn ? 1 : 0));
            if (operand->getType() == expected) arguments.push_back(operand);
            else if (expected->isPointerTy() && isStringType(inst.args[i].type)) {
                auto* slot = allocate(operand->getType(), "call.arg.addr");
                builder_.CreateStore(operand, slot);
                arguments.push_back(slot);
            } else unsupported("call argument ABI mismatch");
        }
        auto* result = builder_.CreateCall(callee, arguments,
            callee->getReturnType()->isVoidTy() ? "" : inst.result.value_or(""));
        if (indirectReturn && inst.result) bind(*inst.result, builder_.CreateLoad(stringType(context_, "String", true, false, enablePairI32_), returnSlot, *inst.result));
        else if (inst.result) bind(*inst.result, result);
    }
    void lowerInstruction(const LirUnaryInst& inst) {
        // The internal LIR opcode is '-', although its text renderer prints neg.
        if (inst.op == "-") {
            if (!enableArithmeticI32_ || inst.type != "Int32" || inst.operand.type != "Int32")
                unsupported("unary operator '-' (Int32 arithmetic is disabled or malformed)");
            // Cat accepts -2147483648: its LIR retains unary '-' and the positive
            // literal magnitude. Permit that literal here only, not as a general
            // out-of-range Int32 value or as an unresolved/typed binding.
            if (!inst.operand.isUnit && inst.operand.bindingKey.empty() && inst.operand.text == "2147483648" &&
                !values_.contains(inst.operand.text) && !slots_.contains(inst.operand.text)) {
                bind(inst.result, llvm::ConstantInt::getSigned(llvm::Type::getInt32Ty(context_),
                     std::numeric_limits<int32_t>::min()));
                return;
            }
            auto* operand = value(inst.operand);
            if (!operand->getType()->isIntegerTy(32)) unsupported("non-Int32 neg operand");
            // Deliberately no NSW/NUW: this PoC implements native release wrapping.
            bind(inst.result, builder_.CreateSub(llvm::ConstantInt::get(operand->getType(), 0), operand, inst.result));
            return;
        }
        if (inst.op != "not" || inst.type != "Bool") unsupported("unary operator '" + inst.op + "'");
        auto* operand = value(inst.operand);
        if (!operand->getType()->isIntegerTy(1)) unsupported("non-Bool not operand");
        bind(inst.result, builder_.CreateNot(operand, inst.result));
    }
    void lowerInstruction(const LirBinaryInst& inst) {
        const bool arithmetic = inst.op == "add" || inst.op == "sub" || inst.op == "mul" ||
                                inst.op == "div" || inst.op == "rem";
        if (arithmetic) {
            if (!enableArithmeticI32_ || inst.type != "Int32" || inst.left.type != "Int32" ||
                inst.right.type != "Int32")
                unsupported("arithmetic operator '" + inst.op + "' requires Int32 operands/result and --arithmetic-i32");
            auto* left = value(inst.left);
            auto* right = value(inst.right);
            if (!left->getType()->isIntegerTy(32) || !right->getType()->isIntegerTy(32))
                unsupported("arithmetic operand type mismatch");
            llvm::Value* result = nullptr;
            if (inst.op == "add") result = builder_.CreateAdd(left, right, inst.result);
            else if (inst.op == "sub") result = builder_.CreateSub(left, right, inst.result);
            else if (inst.op == "mul") result = builder_.CreateMul(left, right, inst.result);
            else {
                if (!runtimeDivisionByZero_) unsupported("division defect runtime is unavailable");
                // Guard before sdiv/srem. The safe divisor also handles MIN/-1,
                // because LLVM's signed division pair is poison/undefined there.
                auto* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0);
                auto* one = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1);
                auto* isZero = builder_.CreateICmpEQ(right, zero, inst.result + ".zero");
                auto* min = llvm::ConstantInt::getSigned(llvm::Type::getInt32Ty(context_), std::numeric_limits<int32_t>::min());
                auto* minusOne = llvm::ConstantInt::getSigned(llvm::Type::getInt32Ty(context_), -1);
                auto* overflow = builder_.CreateAnd(builder_.CreateICmpEQ(left, min),
                    builder_.CreateICmpEQ(right, minusOne), inst.result + ".overflow");
                auto* invalid = builder_.CreateOr(isZero, overflow, inst.result + ".invalid");
                auto* safeRight = builder_.CreateSelect(invalid, one, right, inst.result + ".safe.divisor");
                auto* fail = llvm::BasicBlock::Create(context_, inst.result + ".div.zero", function_.ir);
                auto* ok = llvm::BasicBlock::Create(context_, inst.result + ".div.ok", function_.ir);
                builder_.CreateCondBr(isZero, fail, ok);
                builder_.SetInsertPoint(fail);
                builder_.CreateCall(runtimeDivisionByZero_);
                builder_.CreateUnreachable();
                builder_.SetInsertPoint(ok);
                auto* raw = inst.op == "div" ? builder_.CreateSDiv(left, safeRight, inst.result + ".raw")
                                              : builder_.CreateSRem(left, safeRight, inst.result + ".raw");
                auto* wrapped = inst.op == "div" ? static_cast<llvm::Value*>(min) : static_cast<llvm::Value*>(zero);
                result = builder_.CreateSelect(overflow, wrapped, raw, inst.result);
            }
            bind(inst.result, result);
            return;
        }
        if (inst.op != "eq" && inst.op != "neq" && inst.op != "lt" && inst.op != "lte" &&
            inst.op != "gt" && inst.op != "gte") unsupported("binary operator '" + inst.op + "'");
        if (inst.type != "Bool") unsupported("non-Bool comparison result");
        if (isStringType(inst.left.type) || isStringType(inst.right.type))
            unsupported("String comparison is unsupported");
        auto* left = value(inst.left);
        auto* right = value(inst.right);
        if (left->getType() != right->getType()) unsupported("comparison operand type mismatch");
        if (inst.op != "eq" && inst.op != "neq" && !left->getType()->isIntegerTy(32))
            unsupported("ordered non-Int32 comparison");
        llvm::Value* result = nullptr;
        if (inst.op == "eq") result = builder_.CreateICmpEQ(left, right, inst.result);
        else if (inst.op == "neq") result = builder_.CreateICmpNE(left, right, inst.result);
        else if (inst.op == "lt") result = builder_.CreateICmpSLT(left, right, inst.result);
        else if (inst.op == "lte") result = builder_.CreateICmpSLE(left, right, inst.result);
        else if (inst.op == "gt") result = builder_.CreateICmpSGT(left, right, inst.result);
        else result = builder_.CreateICmpSGE(left, right, inst.result);
        bind(inst.result, result);
    }
    void lowerInstruction(const LirDropInst& inst) {
        rejectProjectionProvenance(inst.provenance);
        lowerStringDrop(inst);
    }
    void lowerInstruction(const LirStoreFieldInst& inst) {
        rejectProjectionProvenance(inst.object.provenance);
        rejectProjectionProvenance(inst.value.provenance);
        const bool singleton = enableSingletonI32_ && isSingletonType(inst.object.type) && inst.field == "0";
        const bool pair = enablePairI32_ && isPairType(inst.object.type) && (inst.field == "0" || inst.field == "1");
        if ((!singleton && !pair) || inst.object.isUnit || inst.value.type != "Int32" || inst.safety != LirSafetyTag::ProvenSafe)
            unsupported("aggregate field construction");
        const auto found = slots_.find(key(inst.object));
        if (found == slots_.end() || found->second->getAllocatedType() !=
                stringType(context_, inst.object.type, enableStrings_, enableSingletonI32_, enablePairI32_))
            unsupported("singleton constructor requires actual local stack storage");
        auto* address = builder_.CreateStructGEP(found->second->getAllocatedType(), found->second, inst.field == "1" ? 1 : 0, pair ? "pair.field.addr" : "singleton.field0.addr");
        builder_.CreateStore(value(inst.value), address);
    }
    void lowerInstruction(const LirFieldInst& inst) {
        rejectProjectionProvenance(inst.provenance);
        const bool singleton = enableSingletonI32_ && isSingletonType(inst.object.type) && inst.field == "0";
        const bool pair = enablePairI32_ && isPairType(inst.object.type) && (inst.field == "0" || inst.field == "1");
        if ((!singleton && !pair) || inst.type != "Int32" || inst.safety != LirSafetyTag::ProvenSafe)
            unsupported("LIR field projection (only explicitly validated aggregate is supported)");
        bind(inst.result, builder_.CreateExtractValue(value(inst.object), {inst.field == "1" ? 1u : 0u}, inst.result));
    }
    void lowerInstruction(const LirDiscardInst& inst) {
        if (!inst.value.isUnit && inst.value.type != "Unit") (void)value(inst.value);
    }
    template<class T> void lowerInstruction(const T&) { unsupported("LIR instruction outside the scalar/control subset"); }

    llvm::LLVMContext& context_;
    llvm::IRBuilder<> builder_;
    FunctionInfo function_;
    const FunctionMap& functions_;
    llvm::Function* entry_;
    llvm::Function* runtimeI32_;
    llvm::Function* runtimeDivisionByZero_;
    bool enableArithmeticI32_;
    bool enableStrings_;
    bool enableSingletonI32_;
    bool enablePairI32_;
    llvm::Module* module_;
    llvm::Function* runtimeStringAlloc_;
    llvm::Function* runtimeStringCopy_;
    llvm::Function* runtimeStringFree_;
    llvm::Function* runtimePrintSlice_;
    unsigned literalId_ = 0;
    std::unordered_map<std::string, llvm::Value*> values_;
    std::unordered_map<std::string, llvm::Value*> parameterAddresses_;
    std::unordered_map<std::string, llvm::Type*> parameterTypes_;
    std::unordered_map<std::string, llvm::AllocaInst*> slots_;
    std::unordered_map<std::string, llvm::BasicBlock*> blocks_;
};

class StagedArtifact {
public:
    StagedArtifact(std::string path, llvm::sys::fs::TempFile&& temporary)
        : path_(std::move(path)), temporary_(std::move(temporary)) {}
    ~StagedArtifact() {
        if (!finalized_) llvm::consumeError(temporary_.discard());
    }
    StagedArtifact(const StagedArtifact&) = delete;
    StagedArtifact& operator=(const StagedArtifact&) = delete;
    static std::unique_ptr<StagedArtifact> stage(const std::string& path, llvm::StringRef bytes) {
        if (std::filesystem::exists(path) && !std::filesystem::is_regular_file(path))
            throw std::runtime_error("Cannot replace non-regular output '" + path + "'");
        auto temporary = llvm::sys::fs::TempFile::create(path + ".%%%%%%.tmp");
        if (!temporary) throw std::runtime_error("Cannot create output '" + path + "': " + llvm::toString(temporary.takeError()));
        auto artifact = std::make_unique<StagedArtifact>(path, std::move(*temporary));
        llvm::raw_fd_ostream stream(artifact->temporary_.FD, false);
        stream.write(bytes.data(), bytes.size());
        stream.flush();
        if (stream.has_error()) {
            const auto failure = stream.error().message();
            stream.clear_error();
            throw std::runtime_error("Cannot write output '" + path + "': " + failure);
        }
        return artifact;
    }
    void publish() {
        finalized_ = true; // TempFile::keep consumes the temporary even on error.
        if (auto error = temporary_.keep(path_))
            throw std::runtime_error("Cannot publish output '" + path_ + "': " + llvm::toString(std::move(error)));
    }
private:
    std::string path_;
    llvm::sys::fs::TempFile temporary_;
    bool finalized_ = false;
};

void emit(const LirProgram& program, const ObjectOptions& options) {
#if !defined(__x86_64__) || !(defined(__linux__) || (defined(_WIN32) && defined(CLAW_LLVM_CPP_POC_UCRT_X64)))
    unsupported("host: native Linux x86_64 or explicitly configured Windows x64 UCRT required");
#else
    if (options.pairCodeGenOptimization != -1 &&
        (!options.enablePairI32 || (options.pairCodeGenOptimization != 0 && options.pairCodeGenOptimization != 2)))
        unsupported("pair codegen profile requires --pair-i32 and exact O0 or O2");
    if (options.objectPath.empty()) unsupported("empty object output path");
    if (!options.irPath.empty() && options.irPath == options.objectPath) unsupported("aliased IR/object output paths");
    if (program.realms.size() != 1) unsupported("multiple LIR modules");
    // Validate the complete typed program before creating LLVM instructions or artifacts.
    if (options.enableSingletonI32) validateSingletonAdmission(program);
    if (options.enablePairI32) {
        if (options.enableSingletonI32 || options.enableStrings || options.enableRuntimeI32) unsupported("--pair-i32 cannot be combined with singleton, String, or runtime modes");
        validatePairAdmission(program, options.enableArithmeticI32);
    }
    static const bool targetReady = [] {
        return !llvm::InitializeNativeTarget() && !llvm::InitializeNativeTargetAsmPrinter();
    }();
    if (!targetReady) throw std::runtime_error("Cannot initialize native LLVM target");
    const std::string triple(claw::codegen::nativeTargetTriple());
    const llvm::Triple targetTriple(triple);
#if defined(_WIN32)
    if (targetTriple.getArch() != llvm::Triple::x86_64 || !targetTriple.isWindowsGNUEnvironment() || !targetTriple.isOSBinFormatCOFF())
        unsupported("target: Windows x64 GNU/UCRT COFF required");
#else
    if (targetTriple.getArch() != llvm::Triple::x86_64 || !targetTriple.isOSLinux() || !targetTriple.isOSBinFormatELF())
        unsupported("target: only native Linux x86_64 ELF is validated");
#endif
    std::string targetError;
    const auto* target = llvm::TargetRegistry::lookupTarget(targetTriple, targetError);
    if (!target) throw std::runtime_error("Cannot resolve LLVM target: " + targetError);
    llvm::TargetOptions targetOptions;
    std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
        targetTriple, "generic", "", targetOptions, llvm::Reloc::PIC_, std::nullopt,
        options.pairCodeGenOptimization == 0 ? llvm::CodeGenOptLevel::None : llvm::CodeGenOptLevel::Default));
    if (!machine) throw std::runtime_error("Cannot create LLVM TargetMachine");
    llvm::LLVMContext context;
    llvm::Module module("claw_llvm_cpp_poc", context);
    module.setTargetTriple(targetTriple);
    const auto layout = machine->createDataLayout();
    if (!layout.isLittleEndian() || layout.getPointerSizeInBits() != 64)
        unsupported("target DataLayout: little-endian 64-bit pointers required");
    module.setDataLayout(layout);
    if (options.enablePairI32) {
        auto* pair = llvm::cast<llvm::StructType>(stringType(context, "Tuple[Int32, Int32]", false, false, true));
        detail::validatePairLayout(layout, pair);
    }
    if (options.enableSingletonI32) {
        auto* singleton = llvm::cast<llvm::StructType>(stringType(context, "Tuple[Int32]", false, true));
        if (singleton->isPacked() || singleton->getNumElements() != 1 ||
            layout.getTypeAllocSize(singleton).getFixedValue() != 4 ||
            layout.getABITypeAlign(singleton).value() != 4 ||
            layout.getStructLayout(singleton)->getElementOffset(0) != 0)
            unsupported("singleton TargetMachine DataLayout requires size4/align4/offset0");
    }
    if (options.enableStrings) {
        auto* slice = stringType(context, "ref Str", true);
        auto* buffer = stringType(context, "String", true);
        if (layout.getTypeAllocSize(slice).getFixedValue() != 16 || layout.getABITypeAlign(slice).value() != 8 ||
            layout.getTypeAllocSize(buffer).getFixedValue() != 24 || layout.getABITypeAlign(buffer).value() != 8)
            unsupported("String/Str target layout differs from paired oracle");
    }

    FunctionMap functions;
    std::vector<FunctionInfo> definitions;
    std::unordered_set<std::string> symbols;
    llvm::Function* entry = nullptr;
    llvm::Function* runtimeI32 = nullptr;
    llvm::Function* runtimeDivisionByZero = nullptr;
    llvm::Function* runtimeStringAlloc = nullptr;
    llvm::Function* runtimeStringCopy = nullptr;
    llvm::Function* runtimeStringFree = nullptr;
    llvm::Function* runtimePrintSlice = nullptr;
    constexpr llvm::StringLiteral runtimeSymbol("claw.runtime.println.i32");
    constexpr llvm::StringLiteral divisionDefectSymbol("claw.runtime.defect.division_by_zero");
    constexpr llvm::StringLiteral stringAllocSymbol("claw.runtime.string.alloc");
    constexpr llvm::StringLiteral stringCopySymbol("claw.runtime.string.copy");
    constexpr llvm::StringLiteral stringFreeSymbol("claw.runtime.string.free");
    constexpr llvm::StringLiteral printSliceSymbol("claw.runtime.println.slice");
    if (options.enableRuntimeI32) {
        for (const auto& declaration : program.realms.front().decls) {
            if (const auto* function = std::get_if<LirFunction>(&declaration);
                function && function->linkage.symbol == runtimeSymbol)
                unsupported("reserved runtime symbol collision 'claw.runtime.println.i32'");
        }
    }
    if (options.enableArithmeticI32) {
        for (const auto& declaration : program.realms.front().decls) {
            if (const auto* function = std::get_if<LirFunction>(&declaration);
                function && function->linkage.symbol == divisionDefectSymbol)
                unsupported("reserved runtime symbol collision 'claw.runtime.defect.division_by_zero'");
        }
    }
    if (options.enableStrings) {
        for (const auto& declaration : program.realms.front().decls) {
            if (const auto* function = std::get_if<LirFunction>(&declaration)) {
                if (function->linkage.symbol == stringAllocSymbol || function->linkage.symbol == stringCopySymbol ||
                    function->linkage.symbol == stringFreeSymbol || function->linkage.symbol == printSliceSymbol)
                    unsupported("reserved string runtime symbol collision");
            }
        }
    }
    for (const auto& declaration : program.realms.front().decls) {
        const auto* function = std::get_if<LirFunction>(&declaration);
        if (!function) {
            if (const auto* choice = std::get_if<LirChoice>(&declaration); choice && isBuiltinTemplate(*choice)) continue;
            unsupported("non-function declaration (shape, choice, or static)");
        }
        const auto& link = function->linkage;
        if (link.abi != "claw" || (link.linkage != LinkageKind::Internal && link.linkage != LinkageKind::Shared))
            unsupported("function linkage/ABI");
        if (link.symbol.empty() || link.symbol == "main" || !symbols.insert(link.symbol).second)
            unsupported("empty, reserved, or duplicate function symbol '" + link.symbol + "'");
        std::vector<llvm::Type*> parameters;
        const bool indirectReturn = function->returnType == "String";
        if (isStringType(function->returnType) && !options.enableStrings)
            unsupported("String ABI while --strings is disabled");
        if (indirectReturn && function->returnPassKind != AbiPassKind::Indirect)
            unsupported("String return requires indirect result slot ABI");
        if (indirectReturn) parameters.push_back(llvm::PointerType::getUnqual(context));
        std::unordered_set<std::string> parameterNames;
        for (const auto& parameter : function->params) {
            if (parameter.name.empty() || !parameterNames.insert(parameter.name).second)
                unsupported("empty or duplicate parameter name");
            auto* type = stringType(context, parameter.type, options.enableStrings, options.enableSingletonI32, options.enablePairI32);
            if (type->isVoidTy()) unsupported("parameter ABI");
            const bool stringParam = isStringType(parameter.type);
            const bool pointerAbi = stringParam && (parameter.type == "String" ||
#if defined(_WIN32)
                parameter.type == "Str" || parameter.type == "ref Str"
#else
                false
#endif
            );
            if (!stringParam && parameter.passKind != ((options.enableSingletonI32 && isSingletonType(parameter.type)) || (options.enablePairI32 && isPairType(parameter.type)) ? AbiPassKind::Direct : AbiPassKind::Scalar)) unsupported("parameter ABI");
            if (stringParam && parameter.passKind != (isBufferType(parameter.type) ? AbiPassKind::Indirect : AbiPassKind::Direct))
                unsupported("string parameter ABI");
            parameters.push_back(pointerAbi ? llvm::PointerType::getUnqual(context) : type);
        }
        auto* result = stringType(context, function->returnType, options.enableStrings, options.enableSingletonI32, options.enablePairI32);
        if (!isStringType(function->returnType) && function->returnPassKind != (result->isVoidTy() ? AbiPassKind::Void : ((options.enableSingletonI32 && isSingletonType(function->returnType)) || (options.enablePairI32 && isPairType(function->returnType)) ? AbiPassKind::Direct : AbiPassKind::Scalar)))
            unsupported("return ABI");
        if (isStringType(function->returnType) && function->returnType != "String" &&
            function->returnPassKind != AbiPassKind::Direct) unsupported("Str return ABI");
        if (indirectReturn) result = llvm::Type::getVoidTy(context);
        auto* signature = llvm::FunctionType::get(result, parameters, false);
        auto* ir = llvm::Function::Create(signature,
            link.linkage == LinkageKind::Internal ? llvm::GlobalValue::InternalLinkage : llvm::GlobalValue::ExternalLinkage,
            link.symbol, module);
        const FunctionInfo info{function, ir};
        if (!functions.emplace(function->name, info).second) unsupported("duplicate local function name");
        if (function->name != link.symbol && !functions.emplace(link.symbol, info).second) unsupported("ambiguous function name");
        definitions.push_back(info);
        if (link.symbol == program.entrySymbol) entry = ir;
    }
    if (options.enableRuntimeI32) {
        if (symbols.contains(runtimeSymbol.str()))
            unsupported("reserved runtime symbol collision 'claw.runtime.println.i32'");
        auto* runtimeType = llvm::FunctionType::get(llvm::Type::getVoidTy(context),
                                                    {llvm::Type::getInt32Ty(context)}, false);
        runtimeI32 = llvm::Function::Create(runtimeType, llvm::GlobalValue::ExternalLinkage,
                                            runtimeSymbol, module);
        runtimeI32->setCallingConv(llvm::CallingConv::C);
    }
    if (options.enableArithmeticI32) {
        auto* defectType = llvm::FunctionType::get(llvm::Type::getVoidTy(context), false);
        runtimeDivisionByZero = llvm::Function::Create(defectType, llvm::GlobalValue::ExternalLinkage,
                                                       divisionDefectSymbol, module);
        runtimeDivisionByZero->setCallingConv(llvm::CallingConv::C);
        runtimeDivisionByZero->setDoesNotReturn();
    }
    if (options.enableStrings) {
        auto* ptr = llvm::PointerType::getUnqual(context);
        runtimeStringAlloc = llvm::Function::Create(llvm::FunctionType::get(ptr, {llvm::Type::getInt64Ty(context)}, false), llvm::GlobalValue::ExternalLinkage, stringAllocSymbol, module);
        runtimeStringCopy = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(context), {ptr, ptr, llvm::Type::getInt64Ty(context)}, false), llvm::GlobalValue::ExternalLinkage, stringCopySymbol, module);
        runtimeStringFree = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(context), {ptr}, false), llvm::GlobalValue::ExternalLinkage, stringFreeSymbol, module);
        runtimePrintSlice = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(context), {ptr}, false), llvm::GlobalValue::ExternalLinkage, printSliceSymbol, module);
    }
    if (!entry || entry->arg_size() != 0 ||
        (!entry->getReturnType()->isVoidTy() && !entry->getReturnType()->isIntegerTy(32)))
        unsupported("entry main signature or symbol");
    for (const auto& info : definitions)
        FunctionLowerer(context, info, functions, entry, runtimeI32, runtimeDivisionByZero,
                        options.enableArithmeticI32, options.enableStrings, options.enableSingletonI32, options.enablePairI32, &module,
                        runtimeStringAlloc, runtimeStringCopy, runtimeStringFree, runtimePrintSlice).lower();

    auto* main = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getInt32Ty(context), false),
                                       llvm::GlobalValue::ExternalLinkage, "main", module);
    llvm::IRBuilder<> wrapper(llvm::BasicBlock::Create(context, "entry", main));
    auto* result = wrapper.CreateCall(entry);
    if (entry->getReturnType()->isVoidTy())
        wrapper.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0));
    else
        wrapper.CreateRet(result);
    std::string verification;
    llvm::raw_string_ostream diagnostic(verification);
    if (llvm::verifyModule(module, &diagnostic))
        throw std::runtime_error("LLVM verifyModule failed: " + diagnostic.str());

    // CodeGen can rewrite zero-offset GEPs and overalign stack slots. Preserve
    // the verified API module for this new slice, not its machine-pass mutation.
    // Keep the historical dump policy byte-invariant when the new flag is OFF.
    std::string singletonIr;
    if (options.enableSingletonI32 && !options.irPath.empty()) {
        llvm::raw_string_ostream stream(singletonIr);
        module.print(stream, nullptr);
    }
    // Generate to memory first: unsupported LIR and verifier failures publish nothing.
    llvm::SmallVector<char, 0> object;
    llvm::raw_svector_ostream objectStream(object);
    llvm::legacy::PassManager passes;
    if (machine->addPassesToEmitFile(passes, objectStream, nullptr, llvm::CodeGenFileType::ObjectFile))
        throw std::runtime_error("LLVM target cannot emit a native object");
    passes.run(module);
    auto stagedObject = StagedArtifact::stage(options.objectPath, llvm::StringRef(object.data(), object.size()));
    std::unique_ptr<StagedArtifact> stagedIr;
    if (!options.irPath.empty()) {
        std::string text = std::move(singletonIr);
        if (!options.enableSingletonI32) {
            llvm::raw_string_ostream stream(text);
            module.print(stream, nullptr);
        }
        stagedIr = StagedArtifact::stage(options.irPath, text);
    }
    // Both outputs have been written/checked before either is published. Each rename
    // is atomic; two distinct paths are not a crash-atomic filesystem transaction.
    stagedObject->publish();
    if (stagedIr) stagedIr->publish();
#endif
}
} // namespace

bool emitObject(const claw::frontend::LirProgram& program, const ObjectOptions& options, std::string& error) {
    error.clear();
    try {
        emit(program, options);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
} // namespace claw::experimental::llvm_cpp
