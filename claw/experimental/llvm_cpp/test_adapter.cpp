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
LirCallInst runtimeCall() {
    LirCallInst result;
    result.callee = "println";
    result.hook = "runtime.println";
    result.args = {{"7", "Int32"}};
    result.type = "Unit";
    result.kind = LirCallKind::Runtime;
    result.safety = LirSafetyTag::ProvenSafe;
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
            {"duplicate_parameter_name_with_distinct_keys", [](auto& p) {
                helper(p); auto& f=std::get<LirFunction>(p.realms.front().decls.back());
                f.params[0].bindingKey="opaque:first";
                f.params.push_back({"x", "Int32", AbiPassKind::Scalar, "opaque:second"});
            }},
            {"duplicate_parameter_key_with_distinct_names", [](auto& p) {
                helper(p); auto& f=std::get<LirFunction>(p.realms.front().decls.back());
                f.params[0].bindingKey="opaque:shared";
                f.params.push_back({"y", "Int32", AbiPassKind::Scalar, "opaque:shared"});
            }},
            {"keyed_parameter_use_missing_key", [](auto& p) {
                helper(p); auto& f=std::get<LirFunction>(p.realms.front().decls.back());
                f.params[0].bindingKey="opaque:formal";
            }},
            {"entry_call", [](auto& p) { auto c = call(); c.callee = "main"; instructions(p, {c}); }},
            {"store_type", [](auto& p) { instructions(p, {LirObjectInst{true, "x", "Int32", "stack", "local:x"}, LirStoreInst{"x", {"false", "Bool"}, "local:x"}}); }},
            {"drop_instruction", [](auto& p) { instructions(p, {LirDropInst{"x", "String", "local:x"}}); }},
            {"signed_add", [](auto& p) { instructions(p, {LirBinaryInst{"%sum", "add", {"1", "Int32"}, {"2", "Int32"}, "Int32"}}); }},
            {"multiple_modules", [](auto& p) { p.realms.push_back({"other", {}}); }},
            {"runtime_default_rejected", [](auto& p) { instructions(p, {runtimeCall(), LirReturnInst{{"7", "Int32"}}}); }},
            {"runtime_wrong_hook", [](auto& p) { auto c = runtimeCall(); c.hook = "runtime.print"; instructions(p, {c, LirReturnInst{{"7", "Int32"}}}); }},
            {"runtime_wrong_callee", [](auto& p) { auto c = runtimeCall(); c.callee = "print"; instructions(p, {c, LirReturnInst{{"7", "Int32"}}}); }},
            {"runtime_arg_count", [](auto& p) { auto c = runtimeCall(); c.args.clear(); instructions(p, {c, LirReturnInst{{"7", "Int32"}}}); }},
            {"runtime_arg_type", [](auto& p) { auto c = runtimeCall(); c.args = {{"true", "Bool"}}; instructions(p, {c, LirReturnInst{{"7", "Int32"}}}); }},
            {"runtime_result", [](auto& p) { auto c = runtimeCall(); c.result = "%bad"; instructions(p, {c, LirReturnInst{{"7", "Int32"}}}); }},
            {"runtime_type", [](auto& p) { auto c = runtimeCall(); c.type = "Int32"; instructions(p, {c, LirReturnInst{{"7", "Int32"}}}); }},
            {"runtime_safety", [](auto& p) { auto c = runtimeCall(); c.safety = LirSafetyTag::None; instructions(p, {c, LirReturnInst{{"7", "Int32"}}}); }},
            {"runtime_metadata", [](auto& p) { auto c = runtimeCall(); c.builtinTag = "println"; instructions(p, {c, LirReturnInst{{"7", "Int32"}}}); }},
            {"runtime_external_metadata", [](auto& p) { auto c = runtimeCall(); c.externalInfo = LirExternalCallInfo{}; instructions(p, {c, LirReturnInst{{"7", "Int32"}}}); }},
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
        {
            auto program=scalarProgram(); helper(program, "Unit");
            auto& f=std::get<LirFunction>(program.realms.front().decls.back());
            f.params[0]={"owner", "String", AbiPassKind::Indirect, "opaque:formal"};
            f.blocks.front().insts={LirObjectInst{false, "different-label", "String", "stack", "opaque:formal"}};
            const auto object=artifacts/"formal_slot_object_collision.o";
            const auto ir=artifacts/"formal_slot_object_collision.ll";
            std::filesystem::remove(object); std::filesystem::remove(ir);
            std::string error;
            require(!emitObject(program, {object.string(),ir.string(),false,false,true},error), "formal pointer/object collision accepted");
            require(error.find("duplicate object binding")!=std::string::npos, "formal collision did not reach duplicate-object guard: "+error);
            require(!std::filesystem::exists(object)&&!std::filesystem::exists(ir), "formal collision published output");
            std::ofstream(artifacts/"formal_slot_object_collision.log")<<error<<'\n';
            std::cout<<"[adapter/reject] formal_slot_object_collision\n";
        }
        std::string error = "old error must be reset";
        auto runtimeProgram = scalarProgram();
        instructions(runtimeProgram, {runtimeCall(), LirReturnInst{{"7", "Int32"}}});
        const auto runtimeObject = artifacts / "runtime_i32.o";
        const auto runtimeIr = artifacts / "runtime_i32.ll";
        std::filesystem::remove(runtimeObject);
        std::filesystem::remove(runtimeIr);
        require(emitObject(runtimeProgram, {runtimeObject.string(), runtimeIr.string(), true}, error), error);
        require(error.empty() && std::filesystem::file_size(runtimeObject) > 0, "runtime Int32 opt-in failed");
        std::ifstream runtimeText(runtimeIr);
        const std::string runtimeIrText((std::istreambuf_iterator<char>(runtimeText)), {});
        require(runtimeIrText.find("@claw.runtime.println.i32") != std::string::npos, "runtime symbol missing");
        auto arithmeticProgram = scalarProgram();
        instructions(arithmeticProgram, {
            LirBinaryInst{"%add", "add", {"1", "Int32"}, {"2", "Int32"}, "Int32"},
            LirReturnInst{{"%add", "Int32"}}
        });
        const auto arithmeticObject = artifacts / "arithmetic_i32.o";
        const auto arithmeticIr = artifacts / "arithmetic_i32.ll";
        std::filesystem::remove(arithmeticObject);
        std::filesystem::remove(arithmeticIr);
        require(emitObject(arithmeticProgram, {arithmeticObject.string(), arithmeticIr.string(), false, true}, error), error);
        std::ifstream arithmeticText(arithmeticIr);
        const std::string arithmeticIrText((std::istreambuf_iterator<char>(arithmeticText)), {});
        require(!arithmeticIrText.empty() && std::filesystem::file_size(arithmeticObject) > 0,
                "Int32 arithmetic artifact missing");
        for (const std::string op : {"add", "sub", "mul", "neg", "div", "rem"}) {
            auto program = scalarProgram();
            helper(program);
            auto& function = std::get<LirFunction>(program.realms.front().decls.back());
            function.params.push_back(LirParam{"y", "Int32", AbiPassKind::Scalar});
            auto& body = function.blocks.front().insts;
            body.clear();
            if (op == "neg") body.push_back(LirUnaryInst{"%arithmetic", "-", {"x", "Int32"}, "Int32"});
            else body.push_back(LirBinaryInst{"%arithmetic", op, {"x", "Int32"}, {"y", "Int32"}, "Int32"});
            body.push_back(LirReturnInst{{"%arithmetic", "Int32"}});
            instructions(program, {call({{"7", "Int32"}, {"3", "Int32"}}), LirReturnInst{{"%call", "Int32"}}});
            const auto obj = artifacts / ("arithmetic_dynamic_" + op + ".o");
            const auto irPath = artifacts / ("arithmetic_dynamic_" + op + ".ll");
            std::filesystem::remove(obj);
            std::filesystem::remove(irPath);
            require(emitObject(program, {obj.string(), irPath.string(), false, true}, error), error);
            std::ifstream text(irPath);
            const std::string irText((std::istreambuf_iterator<char>(text)), {});
            const std::string opcode = op == "neg" ? "sub" : op == "div" ? "sdiv" : op == "rem" ? "srem" : op;
            require(irText.find(opcode + " i32") != std::string::npos, "missing signed dynamic opcode " + op);
            require(irText.find(" nsw ") == std::string::npos && irText.find(" nuw ") == std::string::npos &&
                    irText.find("poison") == std::string::npos, "unsafe overflow IR " + op);
            if (op == "div" || op == "rem")
                require(irText.find("claw.runtime.defect.division_by_zero") != std::string::npos &&
                        irText.find("noreturn") != std::string::npos && irText.find("unreachable") != std::string::npos &&
                        irText.find("safe.divisor") != std::string::npos, "missing guarded division ABI " + op);
        }
        std::vector<std::pair<std::string, std::function<void(LirProgram&)>>> arithmeticNegatives;
        for (const std::string op : {"add", "sub", "mul", "div", "rem"}) {
            arithmeticNegatives.push_back({op + "_left_type", [op](auto& p) {
                instructions(p, {LirBinaryInst{"%bad", op, {"true", "Bool"}, {"1", "Int32"}, "Int32"}});
            }});
            arithmeticNegatives.push_back({op + "_right_type", [op](auto& p) {
                instructions(p, {LirBinaryInst{"%bad", op, {"1", "Int32"}, {"false", "Bool"}, "Int32"}});
            }});
            arithmeticNegatives.push_back({op + "_result_type", [op](auto& p) {
                instructions(p, {LirBinaryInst{"%bad", op, {"1", "Int32"}, {"2", "Int32"}, "Bool"}});
            }});
        }
        arithmeticNegatives.push_back({"neg_operand_type", [](auto& p) {
            instructions(p, {LirUnaryInst{"%bad", "-", {"true", "Bool"}, "Int32"}});
        }});
        arithmeticNegatives.push_back({"neg_result_type", [](auto& p) {
            instructions(p, {LirUnaryInst{"%bad", "-", {"1", "Int32"}, "Bool"}});
        }});
        arithmeticNegatives.push_back({"duplicate_result", [](auto& p) {
            instructions(p, {LirBinaryInst{"%same", "add", {"1", "Int32"}, {"2", "Int32"}, "Int32"},
                             LirBinaryInst{"%same", "sub", {"3", "Int32"}, {"1", "Int32"}, "Int32"}});
        }});
        arithmeticNegatives.push_back({"out_of_range", [](auto& p) {
            instructions(p, {LirBinaryInst{"%bad", "div", {"2147483648", "Int32"}, {"1", "Int32"}, "Int32"}});
        }});
        arithmeticNegatives.push_back({"neg_out_of_range", [](auto& p) {
            instructions(p, {LirUnaryInst{"%bad", "-", {"2147483649", "Int32"}, "Int32"}});
        }});
        arithmeticNegatives.push_back({"unknown_opcode", [](auto& p) {
            instructions(p, {LirBinaryInst{"%bad", "band", {"1", "Int32"}, {"2", "Int32"}, "Int32"}});
        }});
        arithmeticNegatives.push_back({"reserved_defect_symbol", [](auto& p) {
            entry(p).linkage.symbol = "claw.runtime.defect.division_by_zero";
        }});
        for (const auto& [name, mutate] : arithmeticNegatives) {
            auto program = scalarProgram();
            mutate(program);
            const auto obj = artifacts / ("arithmetic_reject_" + name + ".o");
            const auto irPath = artifacts / ("arithmetic_reject_" + name + ".ll");
            std::filesystem::remove(obj);
            std::filesystem::remove(irPath);
            require(!emitObject(program, {obj.string(), irPath.string(), false, true}, error), "accepted malformed arithmetic " + name);
            require(error.find("unsupported") != std::string::npos && !std::filesystem::exists(obj) &&
                    !std::filesystem::exists(irPath), "malformed arithmetic published artifacts " + name);
        }
        auto minLiteral = scalarProgram();
        instructions(minLiteral, {LirUnaryInst{"%min", "-", {"2147483648", "Int32"}, "Int32"},
                                  LirReturnInst{{"%min", "Int32"}}});
        const auto minObject = artifacts / "arithmetic_min_literal.o";
        const auto minIr = artifacts / "arithmetic_min_literal.ll";
        require(emitObject(minLiteral, {minObject.string(), minIr.string(), false, true}, error), error);
        std::ifstream minText(minIr);
        const std::string minIrText((std::istreambuf_iterator<char>(minText)), {});
        require(minIrText.find("ret i32 -2147483648") != std::string::npos, "literal MIN was not preserved");
        auto malformedArithmetic = scalarProgram();
        instructions(malformedArithmetic, {LirBinaryInst{"%bad", "add", {"true", "Bool"}, {"1", "Int32"}, "Int32"}});
        require(!emitObject(malformedArithmetic, { (artifacts / "arithmetic_bad.o").string(), {}, false, true }, error),
                "malformed arithmetic accepted");
        auto collision = scalarProgram();
        entry(collision).linkage.symbol = "claw.runtime.println.i32";
        require(!emitObject(collision, {(artifacts / "collision.o").string(), {}, true}, error), "runtime symbol collision accepted");
        auto stringFrom = [] {
            LirCallInst c;
            c.result = "%owner"; c.callee = "String.from"; c.args = {{"\"hello\"", "ref Str"}};
            c.type = "String"; c.builtinTag = "String.from"; c.kind = LirCallKind::Builtin;
            c.safety = LirSafetyTag::ProvenSafe; c.hook = "builtin.String.from";
            return c;
        };
        auto stringHelper = [](LirProgram& p, const std::string& type = "String") {
            helper(p);
            auto& f = std::get<LirFunction>(p.realms.front().decls.back());
            f.params = {{"x", type, type == "String" ? AbiPassKind::Indirect : AbiPassKind::Direct}};
            f.returnType = type;
            f.returnPassKind = type == "String" ? AbiPassKind::Indirect : AbiPassKind::Direct;
            f.blocks.front().insts = {LirReturnInst{{"x", type}}};
        };
        const std::vector<std::pair<std::string, Mutation>> stringNegatives = {
            {"owned_return_pass", [=](auto& p) { stringHelper(p); std::get<LirFunction>(p.realms.front().decls.back()).returnPassKind = AbiPassKind::Scalar; }},
            {"owned_parameter_pass", [=](auto& p) { stringHelper(p); std::get<LirFunction>(p.realms.front().decls.back()).params[0].passKind = AbiPassKind::Direct; }},
            {"slice_parameter_pass", [=](auto& p) { stringHelper(p, "ref Str"); std::get<LirFunction>(p.realms.front().decls.back()).params[0].passKind = AbiPassKind::Indirect; }},
            {"slice_return_pass", [=](auto& p) { stringHelper(p, "ref Str"); std::get<LirFunction>(p.realms.front().decls.back()).returnPassKind = AbiPassKind::Indirect; }},
            {"unwritten_result_slot", [=](auto& p) { stringHelper(p); std::get<LirFunction>(p.realms.front().decls.back()).blocks.front().insts.clear(); }},
            {"pointer_logical_argument", [=](auto& p) { stringHelper(p); auto c = call({{"\"bad\"", "ref Str"}}); c.type = "String"; instructions(p, {c}); }},
            {"pointer_logical_return", [=](auto& p) { stringHelper(p); auto& f = std::get<LirFunction>(p.realms.front().decls.back()); f.returnType = "Unit"; f.returnPassKind = AbiPassKind::Void; f.blocks.front().insts.clear(); auto c = call(); c.type = "String"; instructions(p, {c}); }},
            {"unknown_drop_key", [](auto& p) { instructions(p, {LirDropInst{"owner", "String", "unknown:owner"}}); }},
            {"unresolved_literal_binding", [=](auto& p) { auto c = stringFrom(); c.args[0].bindingKey = "missing:key"; instructions(p, {c}); }},
            {"aggregate_eq", [](auto& p) { instructions(p, {LirBinaryInst{"%bad", "eq", {"\"a\"", "ref Str"}, {"\"a\"", "ref Str"}, "Bool"}}); }},
            {"from_wrong_hook", [=](auto& p) { auto c = stringFrom(); c.hook = "builtin.String.clone"; instructions(p, {c}); }},
            {"from_wrong_tag", [=](auto& p) { auto c = stringFrom(); c.builtinTag = "String.unproved"; instructions(p, {c}); }},
            {"from_unsafe", [=](auto& p) { auto c = stringFrom(); c.safety = LirSafetyTag::None; instructions(p, {c}); }},
            {"from_external", [=](auto& p) { auto c = stringFrom(); c.externalInfo = LirExternalCallInfo{}; instructions(p, {c}); }},
            {"unbound_constructor", [=](auto& p) { auto c = stringFrom(); c.result.reset(); instructions(p, {c}); }},
            {"from_wrong_arg", [=](auto& p) { auto c = stringFrom(); c.args = {{"1", "Int32"}}; instructions(p, {c}); }},
            {"clone_missing_receiver", [=](auto& p) { auto c = stringFrom(); c.builtinTag = "String.clone"; c.hook = "builtin.String.clone"; c.args.clear(); instructions(p, {c}); }},
            {"reserved_alloc", [](auto& p) { entry(p).linkage.symbol = "claw.runtime.string.alloc"; }},
            {"reserved_copy", [](auto& p) { entry(p).linkage.symbol = "claw.runtime.string.copy"; }},
            {"reserved_free", [](auto& p) { entry(p).linkage.symbol = "claw.runtime.string.free"; }},
            {"reserved_print", [](auto& p) { entry(p).linkage.symbol = "claw.runtime.println.slice"; }},
            {"duplicate_indirect_params", [=](auto& p) { stringHelper(p); auto& f = std::get<LirFunction>(p.realms.front().decls.back()); f.params.push_back(f.params.front()); }},
            {"forged_indirect_load_type", [=](auto& p) { stringHelper(p); auto& f = std::get<LirFunction>(p.realms.front().decls.back()); f.returnType = "ref Str"; f.returnPassKind = AbiPassKind::Direct; f.blocks.front().insts = {LirReturnInst{{"x", "ref Str"}}}; }},
        };
        for (const auto& [name, mutate] : stringNegatives) {
            auto p = scalarProgram(); mutate(p);
            const auto obj = artifacts / ("string_reject_" + name + ".o");
            const auto irPath = artifacts / ("string_reject_" + name + ".ll");
            std::filesystem::remove(obj); std::filesystem::remove(irPath);
            require(!emitObject(p, {obj.string(), irPath.string(), false, false, true}, error), "accepted malformed String " + name);
            require(!error.empty() && !std::filesystem::exists(obj) && !std::filesystem::exists(irPath), "String rejection published artifacts " + name);
            std::ofstream(artifacts / ("string_reject_" + name + ".log")) << error << '\n';
        }
        auto stringProgram = scalarProgram(); stringHelper(stringProgram);
        auto transfer = call({{"%owner", "String"}}); transfer.type = "String";
        instructions(stringProgram, {stringFrom(), transfer, LirDropInst{"%call", "String", {}}, LirReturnInst{{"7", "Int32"}}});
        const auto stringObject = artifacts / "string_indirect_transfer.o";
        const auto stringIr = artifacts / "string_indirect_transfer.ll";
        require(emitObject(stringProgram, {stringObject.string(), stringIr.string(), false, false, true}, error), error);
        require(std::filesystem::file_size(stringObject) > 0, "String transfer artifact missing");
        auto bytes = [](const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(input), {});
        };
        const auto savedStringObject = bytes(stringObject);
        const auto savedStringIr = bytes(stringIr);
        auto rejectedString = scalarProgram();
        instructions(rejectedString, {LirDropInst{"missing", "String", "missing:key"}});
        require(!emitObject(rejectedString, {stringObject.string(), stringIr.string(), false, false, true}, error), "bad String recompile accepted");
        require(bytes(stringObject) == savedStringObject && bytes(stringIr) == savedStringIr, "String failure replaced prior object/IR");
        auto sliceProgram = scalarProgram(); stringHelper(sliceProgram, "ref Str");
        auto sliceCall = call({{"\"Aé\\0Z\"", "ref Str"}}); sliceCall.type = "ref Str";
        instructions(sliceProgram, {sliceCall, LirReturnInst{{"7", "Int32"}}});
        require(emitObject(sliceProgram, {(artifacts / "slice_transfer.o").string(), (artifacts / "slice_transfer.ll").string(), false, false, true}, error), error);
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
        std::cout << "PASS: " << stringNegatives.size() << " aggregate ABI/builtin/binding rejections; direct Str and indirect String API objects\n";
        std::cout << "PASS: " << negatives.size() << " legacy malformed-LIR rejections, " << arithmeticNegatives.size()
                  << " arithmetic rejections, six dynamic arithmetic opcodes, valid object, stale-error reset, preserved prior output\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
