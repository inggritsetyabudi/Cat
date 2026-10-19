#include "experimental/llvm_cpp/adapter.h"
#include "codegen/llvm.h"

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
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#if LLVM_VERSION_MAJOR != 23 || LLVM_VERSION_MINOR != 1 || LLVM_VERSION_PATCH != 3
#error "The experimental Cat adapter requires exact LLVM 23.1.3 headers"
#endif

namespace claw::experimental::llvm_cpp {
namespace {
using namespace claw::frontend;

[[noreturn]] void unsupported(const std::string& detail) {
    throw std::runtime_error("LLVM C++ PoC unsupported " + detail);
}

llvm::Type* scalarType(llvm::LLVMContext& context, const std::string& name) {
    // Deliberately do not erase ref/view prefixes or reinterpret aggregate ABI.
    if (name == "Int32") return llvm::Type::getInt32Ty(context);
    if (name == "Bool") return llvm::Type::getInt1Ty(context);
    if (name == "Unit") return llvm::Type::getVoidTy(context);
    unsupported("scalar type '" + name + "'");
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
                    const FunctionMap& functions, llvm::Function* entry)
        : context_(context), builder_(context), function_(function), functions_(functions), entry_(entry) {}

    void lower() {
        for (const auto& block : function_.lir->blocks) {
            if (!blocks_.emplace(block.label, llvm::BasicBlock::Create(context_, block.label, function_.ir)).second)
                unsupported("duplicate block label '" + block.label + "'");
        }
        if (blocks_.empty()) unsupported("function without blocks");
        for (size_t i = 0; i < function_.lir->params.size(); ++i) {
            const auto& name = function_.lir->params[i].name;
            auto* argument = function_.ir->getArg(i);
            argument->setName(name);
            if (!values_.emplace(name, argument).second) unsupported("duplicate parameter '" + name + "'");
        }
        for (const auto& block : function_.lir->blocks) {
            builder_.SetInsertPoint(blocks_.at(block.label));
            for (const auto& instruction : block.insts) {
                if (builder_.GetInsertBlock()->getTerminatorOrNull()) unsupported("instruction after terminator");
                std::visit([this](const auto& inst) { lowerInstruction(inst); }, instruction);
            }
            if (!builder_.GetInsertBlock()->getTerminatorOrNull()) {
                if (!function_.ir->getReturnType()->isVoidTy()) unsupported("unterminated non-Unit block '" + block.label + "'");
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
    llvm::Value* value(const LirValue& input) {
        auto* expectedType = scalarType(context_, input.type);
        if (input.isUnit || expectedType->isVoidTy()) unsupported("Unit as a scalar operand");
        llvm::Value* result = nullptr;
        const auto id = key(input);
        if (const auto found = slots_.find(id); found != slots_.end()) {
            result = builder_.CreateLoad(found->second->getAllocatedType(), found->second, input.text + ".load");
        } else if (const auto found = values_.find(id); found != values_.end()) {
            result = found->second;
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
        if (!values_.emplace(name, result).second || slots_.contains(name)) unsupported("duplicate SSA result '" + name + "'");
    }
    void lowerInstruction(const LirObjectInst& inst) {
        auto* type = scalarType(context_, inst.type);
        if (type->isVoidTy()) unsupported("Unit local storage");
        const auto id = inst.bindingKey.empty() ? inst.name : inst.bindingKey;
        if (slots_.contains(id) || values_.contains(id)) unsupported("duplicate object binding '" + id + "'");
        if (inst.storage != "stack") unsupported("object storage '" + inst.storage + "'");
        // Hoist storage, not initialization, so nested/loop-local allocas dominate uses.
        llvm::IRBuilder<> allocation(&function_.ir->getEntryBlock(), function_.ir->getEntryBlock().begin());
        slots_.emplace(id, allocation.CreateAlloca(type, nullptr, inst.name + ".slot"));
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
            if (!inst.value.isUnit && inst.value.type != "Unit") unsupported("value return in Unit function");
            builder_.CreateRetVoid();
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
    void lowerInstruction(const LirPhiInst& inst) {
        // Current Cat LIR uses Control phi as metadata. Value phis are not covered.
        if (inst.type != "Control") unsupported("value phi instruction");
    }
    void lowerInstruction(const LirCallInst& inst) {
        if ((inst.kind != LirCallKind::Direct && inst.kind != LirCallKind::ModuleDirect) ||
            inst.externalInfo || inst.builtinTag || inst.builtinReceiver)
            unsupported("non-direct call '" + inst.callee + "'");
        const auto found = functions_.find(inst.callee);
        if (found == functions_.end()) unsupported("unknown call target '" + inst.callee + "'");
        auto* callee = found->second.ir;
        if (callee == entry_) unsupported("call to entry main");
        if (callee == function_.ir) unsupported("recursive direct call");
        if (inst.args.size() != callee->arg_size()) unsupported("call argument count mismatch");
        if (scalarType(context_, inst.type) != callee->getReturnType()) unsupported("call result type mismatch");
        std::vector<llvm::Value*> arguments;
        for (size_t i = 0; i < inst.args.size(); ++i) {
            auto* operand = value(inst.args[i]);
            if (operand->getType() != callee->getFunctionType()->getParamType(i)) unsupported("call argument ABI mismatch");
            arguments.push_back(operand);
        }
        auto* result = builder_.CreateCall(callee, arguments,
            callee->getReturnType()->isVoidTy() ? "" : inst.result.value_or(""));
        if (inst.result) bind(*inst.result, result);
    }
    void lowerInstruction(const LirUnaryInst& inst) {
        if (inst.op != "not" || inst.type != "Bool") unsupported("unary operator '" + inst.op + "'");
        auto* operand = value(inst.operand);
        if (!operand->getType()->isIntegerTy(1)) unsupported("non-Bool not operand");
        bind(inst.result, builder_.CreateNot(operand, inst.result));
    }
    void lowerInstruction(const LirBinaryInst& inst) {
        // No arithmetic is emitted until both language profiles/defects are proved.
        if (inst.op != "eq" && inst.op != "neq" && inst.op != "lt" && inst.op != "lte" &&
            inst.op != "gt" && inst.op != "gte") unsupported("binary operator '" + inst.op + "'");
        if (inst.type != "Bool") unsupported("non-Bool comparison result");
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
    void lowerInstruction(const LirDiscardInst& inst) {
        if (!inst.value.isUnit && inst.value.type != "Unit") (void)value(inst.value);
    }
    template<class T> void lowerInstruction(const T&) { unsupported("LIR instruction outside the scalar/control subset"); }

    llvm::LLVMContext& context_;
    llvm::IRBuilder<> builder_;
    FunctionInfo function_;
    const FunctionMap& functions_;
    llvm::Function* entry_;
    std::unordered_map<std::string, llvm::Value*> values_;
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
#if !defined(__linux__) || !defined(__x86_64__)
    unsupported("host: only native Linux x86_64 is validated");
#else
    if (options.objectPath.empty()) unsupported("empty object output path");
    if (!options.irPath.empty() && options.irPath == options.objectPath) unsupported("aliased IR/object output paths");
    if (program.realms.size() != 1) unsupported("multiple LIR modules");
    static const bool targetReady = [] {
        return !llvm::InitializeNativeTarget() && !llvm::InitializeNativeTargetAsmPrinter();
    }();
    if (!targetReady) throw std::runtime_error("Cannot initialize native LLVM target");
    const std::string triple(claw::codegen::nativeTargetTriple());
    const llvm::Triple targetTriple(triple);
    if (targetTriple.getArch() != llvm::Triple::x86_64 || !targetTriple.isOSLinux() || !targetTriple.isOSBinFormatELF())
        unsupported("target: only native Linux x86_64 ELF is validated");
    std::string targetError;
    const auto* target = llvm::TargetRegistry::lookupTarget(targetTriple, targetError);
    if (!target) throw std::runtime_error("Cannot resolve LLVM target: " + targetError);
    llvm::TargetOptions targetOptions;
    std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
        targetTriple, "generic", "", targetOptions, llvm::Reloc::PIC_));
    if (!machine) throw std::runtime_error("Cannot create LLVM TargetMachine");
    llvm::LLVMContext context;
    llvm::Module module("claw_llvm_cpp_poc", context);
    module.setTargetTriple(targetTriple);
    const auto layout = machine->createDataLayout();
    if (!layout.isLittleEndian() || layout.getPointerSizeInBits() != 64)
        unsupported("target DataLayout: little-endian 64-bit pointers required");
    module.setDataLayout(layout);

    FunctionMap functions;
    std::vector<FunctionInfo> definitions;
    std::unordered_set<std::string> symbols;
    llvm::Function* entry = nullptr;
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
        for (const auto& parameter : function->params) {
            auto* type = scalarType(context, parameter.type);
            if (type->isVoidTy() || parameter.passKind != AbiPassKind::Scalar) unsupported("parameter ABI");
            parameters.push_back(type);
        }
        auto* result = scalarType(context, function->returnType);
        if (function->returnPassKind != (result->isVoidTy() ? AbiPassKind::Void : AbiPassKind::Scalar))
            unsupported("return ABI");
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
    if (!entry || entry->arg_size() != 0 ||
        (!entry->getReturnType()->isVoidTy() && !entry->getReturnType()->isIntegerTy(32)))
        unsupported("entry main signature or symbol");
    for (const auto& info : definitions) FunctionLowerer(context, info, functions, entry).lower();

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
        std::string text;
        llvm::raw_string_ostream stream(text);
        module.print(stream, nullptr);
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
