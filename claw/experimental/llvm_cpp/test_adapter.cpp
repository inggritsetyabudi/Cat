#include "adapter.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace claw::frontend;
using claw::experimental::llvm_cpp::ObjectOptions;
using claw::experimental::llvm_cpp::emitObject;

namespace {
LirProgram scalarProgram() {
    LirFunction entry;
    entry.name = "main";
    entry.returnType = "Int32";
    entry.returnPassKind = AbiPassKind::Scalar;
    entry.linkage.symbol = "test::main";
    entry.blocks.push_back(LirBlock{"entry", {}, {}, {LirReturnInst{LirValue{"7", "Int32"}}}});
    return LirProgram{"test", "test::main", {LirRealm{"test", {entry}}}};
}
LirFunction& entry(LirProgram& program) {
    return std::get<LirFunction>(program.realms.front().decls.front());
}
void instructions(LirProgram& program, std::vector<LirInst> values) {
    entry(program).blocks.front().insts = std::move(values);
}
LirCallInst call(std::vector<LirValue> arguments = {}) {
    LirCallInst result;
    result.result = "%call";
    result.callee = "helper";
    result.args = std::move(arguments);
    result.type = "Int32";
    return result;
}
void helper(LirProgram& program, const std::string& resultType = "Int32") {
    LirFunction function;
    function.name = "helper";
    function.returnType = resultType;
    function.returnPassKind = resultType == "Unit" ? AbiPassKind::Void : AbiPassKind::Scalar;
    function.linkage.symbol = "test::helper";
    function.params.push_back(LirParam{"x", "Int32", AbiPassKind::Scalar});
    if (resultType == "Unit") function.blocks.push_back(LirBlock{"entry", {}, {}, {}});
    else function.blocks.push_back(LirBlock{"entry", {}, {}, {LirReturnInst{LirValue{"x", "Int32"}}}});
    program.realms.front().decls.push_back(function);
}
void require(bool condition, const std::string& reason) {
    if (!condition) throw std::runtime_error(reason);
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "expected artifact directory argument");
        const std::filesystem::path artifacts(argv[1]);
        std::filesystem::create_directories(artifacts);
        using Mutation = std::function<void(LirProgram&)>;
        const std::vector<std::pair<std::string, Mutation>> negatives = {
            {"entry_params", [](auto& p) { entry(p).params.push_back({"x", "Int32", AbiPassKind::Scalar}); }},
            {"entry_bool", [](auto& p) { entry(p).returnType = "Bool"; }},
            {"duplicate_symbol", [](auto& p) { p.realms.front().decls.push_back(entry(p)); }},
            {"missing_entry", [](auto& p) { p.entrySymbol = "test::absent"; }},
            {"foreign_abi", [](auto& p) { entry(p).linkage.abi = "c"; }},
            {"indirect_abi", [](auto& p) { entry(p).returnPassKind = AbiPassKind::Indirect; }},
            {"reference_type", [](auto& p) { entry(p).returnType = "ref Int32"; }},
            {"unsigned_type", [](auto& p) { entry(p).returnType = "UInt32"; }},
            {"duplicate_block", [](auto& p) { entry(p).blocks.push_back(entry(p).blocks.front()); }},
            {"unknown_goto", [](auto& p) { instructions(p, {LirGotoInst{"missing"}}); }},
            {"non_bool_branch", [](auto& p) { instructions(p, {LirBranchInst{{"1", "Int32"}, "entry", "entry"}}); }},
            {"wrong_return", [](auto& p) { instructions(p, {LirReturnInst{{"true", "Bool"}}}); }},
            {"literal_overflow", [](auto& p) { instructions(p, {LirReturnInst{{"4294967296", "Int32"}}}); }},
            {"literal_bool_as_int", [](auto& p) { instructions(p, {LirReturnInst{{"true", "Int32"}}}); }},
            {"after_terminator", [](auto& p) { entry(p).blocks.front().insts.push_back(LirGotoInst{"entry"}); }},
            {"value_phi", [](auto& p) { instructions(p, {LirPhiInst{"%p", "Int32", {}}}); }},
            {"unknown_call", [](auto& p) { instructions(p, {call()}); }},
            {"call_arity", [](auto& p) { helper(p); instructions(p, {call()}); }},
            {"call_type", [](auto& p) { helper(p); instructions(p, {call({{"false", "Bool"}})}); }},
            {"call_result_type", [](auto& p) { helper(p); auto c = call({{"3", "Int32"}}); c.type = "Bool"; instructions(p, {c}); }},
            {"unit_result_binding", [](auto& p) { helper(p, "Unit"); auto c = call({{"3", "Int32"}}); c.type = "Unit"; instructions(p, {c}); }},
            {"entry_call", [](auto& p) { auto c = call(); c.callee = "main"; instructions(p, {c}); }},
            {"store_type", [](auto& p) { instructions(p, {LirObjectInst{true, "x", "Int32", "stack", "local:x"}, LirStoreInst{"x", {"false", "Bool"}, "local:x"}}); }},
            {"drop_instruction", [](auto& p) { instructions(p, {LirDropInst{"x", "String", "local:x"}}); }},
            {"signed_add", [](auto& p) { instructions(p, {LirBinaryInst{"%sum", "add", {"1", "Int32"}, {"2", "Int32"}, "Int32"}}); }},
            {"multiple_modules", [](auto& p) { p.realms.push_back({"other", {}}); }},
        };
        for (const auto& [name, mutate] : negatives) {
            auto program = scalarProgram();
            mutate(program);
            const auto object = artifacts / (name + ".o");
            const auto ir = artifacts / (name + ".ll");
            std::filesystem::remove(object);
            std::filesystem::remove(ir);
            std::string error;
            require(!emitObject(program, {object.string(), ir.string()}, error), name + ": unexpectedly accepted");
            require(!error.empty(), name + ": missing diagnostic");
            require(!std::filesystem::exists(object) && !std::filesystem::exists(ir), name + ": published rejected LIR");
            std::ofstream(artifacts / (name + ".log")) << error << '\n';
            std::cout << "[adapter/reject] " << name << '\n';
        }
        std::string error = "old error must be reset";
        const auto object = artifacts / "valid.o";
        const auto ir = artifacts / "valid.ll";
        std::filesystem::remove(object);
        std::filesystem::remove(ir);
        require(emitObject(scalarProgram(), {object.string(), ir.string()}, error), error);
        require(error.empty() && std::filesystem::file_size(object) > 0, "valid object/error-reset failed");
        // A rejected recompile must leave the existing valid output untouched.
        const auto size = std::filesystem::file_size(object);
        auto bad = scalarProgram();
        entry(bad).returnType = "Float64";
        require(!emitObject(bad, {object.string(), ir.string()}, error), "recompile unexpectedly accepted");
        require(std::filesystem::file_size(object) == size, "failed recompile changed existing object");
        std::cout << "PASS: " << negatives.size() << " malformed-LIR rejections, valid object, stale-error reset, preserved prior output\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
