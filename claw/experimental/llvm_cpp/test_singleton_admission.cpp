#include "adapter.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace claw::frontend;
using claw::experimental::llvm_cpp::ObjectOptions;
using claw::experimental::llvm_cpp::emitObject;

namespace {
using Path = std::filesystem::path;
using Mutation = std::function<void(LirProgram&)>;

void need(bool ok, const std::string& why) {
    if (!ok) throw std::runtime_error("singleton admission: " + why);
}

LirValue value(std::string text, std::string type, std::string key = {}, bool isUnit = false) {
    LirValue result{std::move(text), std::move(type)};
    result.bindingKey = std::move(key);
    result.isUnit = isUnit;
    return result;
}

LirValue keyed(std::string text, std::string type, std::string key) {
    return value(std::move(text), std::move(type), std::move(key));
}

LirFunction& fn(LirProgram& program, const std::string& name) {
    for (auto& declaration : program.realms.front().decls) {
        if (auto* function = std::get_if<LirFunction>(&declaration); function && function->name == name)
            return *function;
    }
    throw std::runtime_error("missing function " + name);
}

LirObjectInst object(const std::string& name = "%t0", const std::string& key = "slot:t0") {
    LirObjectInst result;
    result.name = name;
    result.type = "Tuple[Int32]";
    result.storage = "stack";
    result.bindingKey = key;
    return result;
}

LirFieldInst field(const std::string& result, const std::string& obj,
                  const std::string& type = "Tuple[Int32]", const std::string& fieldName = "0",
                  const std::string& objectKey = {}) {
    LirFieldInst instruction;
    instruction.result = result;
    instruction.object = keyed(obj, type, objectKey.empty() ? obj : objectKey);
    instruction.field = fieldName;
    instruction.type = "Int32";
    return instruction;
}

LirProgram valid() {
    LirFunction make;
    make.name = "make";
    make.returnType = "Tuple[Int32]";
    make.returnPassKind = AbiPassKind::Direct;
    make.linkage.symbol = "singleton::make";
    make.params.push_back({"x", "Int32", AbiPassKind::Scalar, "param:x"});
    make.blocks.push_back({"entry", {}, {}, {
        object(),
        LirStoreFieldInst{keyed("%t0", "Tuple[Int32]", "slot:t0"), "0",
                          keyed("x", "Int32", "param:x"), LirSafetyTag::ProvenSafe},
        LirReturnInst{keyed("%t0", "Tuple[Int32]", "slot:t0")}
    }});

    LirFunction take;
    take.name = "take";
    take.returnType = "Int32";
    take.returnPassKind = AbiPassKind::Scalar;
    take.linkage.symbol = "singleton::take";
    take.params.push_back({"p", "Tuple[Int32]", AbiPassKind::Direct, "param:p"});
    take.blocks.push_back({"entry", {}, {}, {
        field("%f", "p", "Tuple[Int32]", "0", "param:p"),
        LirReturnInst{keyed("%f", "Int32", "%f")}
    }});

    LirFunction main;
    main.name = "main";
    main.returnType = "Int32";
    main.returnPassKind = AbiPassKind::Scalar;
    main.linkage.symbol = "singleton::main";
    main.blocks.push_back({"entry", {}, {}, {
        object("p", "slot:p"),
        LirCallInst{"%made", "make", {value("42", "Int32")}, "Tuple[Int32]", {}, LirCallKind::Direct},
        LirStoreInst{"p", keyed("%made", "Tuple[Int32]", "%made"), "slot:p"},
        field("%projected", "p", "Tuple[Int32]", "0", "slot:p"),
        LirCallInst{"%taken", "take", {keyed("p", "Tuple[Int32]", "slot:p")}, "Int32", {}, LirCallKind::Direct},
        LirReturnInst{keyed("%taken", "Int32", "%taken")}
    }});

    for (LirFunction* function : {&make, &take, &main}) {
        function->linkage.abi = "claw";
        function->linkage.linkage = LinkageKind::Internal;
        function->linkage.ffiStable = false;
    }
    return LirProgram{"singleton", "singleton::main", {{"singleton", {make, take, main}}}};
}

LirProgram sourceShapedScalar() {
    LirFunction main;
    main.name = "main";
    main.returnType = "Int32";
    main.returnPassKind = AbiPassKind::Scalar;
    main.linkage.symbol = "singleton::main";
    main.linkage.abi = "claw";
    main.linkage.linkage = LinkageKind::Internal;
    main.linkage.ffiStable = false;
    main.blocks.push_back({"entry", {}, {}, {LirReturnInst{value("7", "Int32")}}});
    return LirProgram{"singleton", "singleton::main", {{"singleton", {main}}}};
}

LirProgram unitProgram() {
    LirFunction main;
    main.name = "main";
    main.returnType = "Unit";
    main.returnPassKind = AbiPassKind::Void;
    main.linkage.symbol = "singleton::main";
    main.linkage.abi = "claw";
    main.linkage.linkage = LinkageKind::Internal;
    main.linkage.ffiStable = false;
    main.blocks.push_back({"entry", {}, {}, {LirReturnInst{value("unit", "Unit", {}, true)}}});
    return LirProgram{"singleton", "singleton::main", {{"singleton", {main}}}};
}

LirProgram validChain() {
    auto program = valid();
    auto& main = fn(program, "main");
    auto instructions = std::move(main.blocks.front().insts);
    LirInst last = std::move(instructions.back());
    instructions.pop_back();
    instructions.push_back(LirGotoInst{"finish"});
    main.blocks.front().insts = std::move(instructions);
    main.blocks.push_back({"finish", {}, {"entry"}, {std::move(last)}});
    return program;
}

ObjectOptions options(const Path& directory, bool singleton,
                      bool runtime = false, bool arithmetic = false, bool strings = false) {
    ObjectOptions result;
    result.objectPath = (directory / "out.o").string();
    result.irPath = (directory / "out.ll").string();
    result.enableRuntimeI32 = runtime;
    result.enableArithmeticI32 = arithmetic;
    result.enableStrings = strings;
    result.enableSingletonI32 = singleton;
    return result;
}

std::vector<unsigned char> bytes(const Path& path) {
    std::ifstream input(path, std::ios::binary);
    need(input.good(), "cannot read artifact " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void removeOutputs(const ObjectOptions& output) {
    std::error_code ignored;
    std::filesystem::remove(output.objectPath, ignored);
    std::filesystem::remove(output.irPath, ignored);
}

void writeSentinels(const ObjectOptions& output) {
    const std::array<unsigned char, 9> objectSentinel{{0, 0x7f, 0, 0, 0xff, 'o', 'b', 'j', 0}};
    const std::array<unsigned char, 8> irSentinel{{0, 0x01, 0x02, 0, 'i', 'r', 0xff, 0}};
    std::ofstream objectFile(output.objectPath, std::ios::binary);
    std::ofstream irFile(output.irPath, std::ios::binary);
    objectFile.write(reinterpret_cast<const char*>(objectSentinel.data()), objectSentinel.size());
    irFile.write(reinterpret_cast<const char*>(irSentinel.data()), irSentinel.size());
}

void checkAbsent(const ObjectOptions& output, const std::string& name) {
    need(!std::filesystem::exists(output.objectPath) && !std::filesystem::exists(output.irPath),
         "published rejected " + name);
}

void expectEmit(const LirProgram& program, const ObjectOptions& output, bool expected,
                std::string& error, const std::string& name) {
    if (expected) std::filesystem::create_directories(Path(output.objectPath).parent_path());
    error.clear();
    const bool emitted = emitObject(program, output, error);
    if (expected) need(emitted, name + ": " + (error.empty() ? "emission failed without a diagnostic" : error));
    else need(!emitted && error.find("unsupported") != std::string::npos,
              "accepted " + name + " or failed outside the admission boundary: " + error);
}

void rejectOne(const Path& root, const std::string& name, const LirProgram& program,
               const ObjectOptions& output, std::ofstream& log) {
    std::filesystem::create_directories(output.objectPath.empty() ? root : Path(output.objectPath).parent_path());
    removeOutputs(output);
    std::string error;
    expectEmit(program, output, false, error, name);
    checkAbsent(output, name);

    writeSentinels(output);
    const auto objectBefore = bytes(output.objectPath);
    const auto irBefore = bytes(output.irPath);
    expectEmit(program, output, false, error, name + " with sentinels");
    need(bytes(output.objectPath) == objectBefore && bytes(output.irPath) == irBefore,
         "clobbered binary sentinel " + name);
    for (const auto& artifact : std::filesystem::directory_iterator(Path(output.objectPath).parent_path()))
        need(artifact.path().extension() != ".tmp", "temporary artifact remained: " + artifact.path().string());
    log << name << "\t" << error << "\n";
}

void reject(const Path& root, const std::string& name, const Mutation& mutate,
            std::ofstream& log, bool oldFlags = false) {
    auto program = valid();
    mutate(program);
    const Path directory = root / name;
    const auto output = options(directory, true, oldFlags, oldFlags, oldFlags);
    rejectOne(root, name, program, output, log);
}

} // namespace

int main(int argc, char** argv) {
    try {
        need(argc == 2, "expected artifact directory");
        const Path root = argv[1];
        std::filesystem::create_directories(root);
        std::ofstream log(root / "admission.log", std::ios::trunc);
        need(log.good(), "cannot create admission log");

        auto positive = valid();
        const Path positiveDirectory = root / "positive-keyed";
        auto positiveOptions = options(positiveDirectory, true);
        removeOutputs(positiveOptions);
        std::string error;
        expectEmit(positive, positiveOptions, true, error, "positive keyed");
        need(std::filesystem::exists(positiveOptions.objectPath) && std::filesystem::exists(positiveOptions.irPath),
             "positive keyed artifacts missing");
        std::ifstream ir(positiveOptions.irPath);
        const std::string text((std::istreambuf_iterator<char>(ir)), {});
        for (const std::string& marker : {
                 "define internal { i32 } @\"singleton::make\"(i32",
                 "define internal i32 @\"singleton::take\"({ i32 }",
                 "call { i32 } @\"singleton::make\"",
                 "call i32 @\"singleton::take\"",
                 "getelementptr inbounds nuw { i32 }",
                 "alloca { i32 }, align 4",
                 "i32 0, i32 0"})
            need(text.find(marker) != std::string::npos, "missing positive IR marker: " + marker);
        log << "positive-keyed\n";

        auto source = sourceShapedScalar();
        const auto sourceOptions = options(root / "positive-source-shaped", true);
        removeOutputs(sourceOptions);
        expectEmit(source, sourceOptions, true, error, "positive source-shaped scalar");
        log << "positive-source-shaped\n";

        auto unit = unitProgram();
        const auto unitOptions = options(root / "positive-unit", true);
        removeOutputs(unitOptions);
        expectEmit(unit, unitOptions, true, error, "positive Unit return");
        log << "positive-unit\n";

        auto chain = validChain();
        const auto chainOptions = options(root / "positive-goto-chain", true);
        removeOutputs(chainOptions);
        expectEmit(chain, chainOptions, true, error, "positive goto chain");
        log << "positive-goto-chain\n";

        auto defaultProgram = valid();
        const auto defaultOptions = options(root / "default", false);
        rejectOne(root, "default singleton disabled", defaultProgram, defaultOptions, log);

        const std::array<std::pair<std::string, std::array<bool, 3>>, 4> oldModes{{
            {"runtime-i32", {true, false, false}},
            {"arithmetic-i32", {false, true, false}},
            {"strings", {false, false, true}},
            {"all-old-flags", {true, true, true}}
        }};
        for (const auto& [mode, flags] : oldModes) {
            rejectOne(root, "default-" + mode, defaultProgram,
                      options(root / ("default-" + mode), false, flags[0], flags[1], flags[2]), log);
        }

        std::vector<std::pair<std::string, Mutation>> bad;
        bad.push_back({"field-index", [](auto& p) {
            fn(p, "make").blocks[0].insts[1] = LirStoreFieldInst{
                keyed("%t0", "Tuple[Int32]", "slot:t0"), "1", keyed("x", "Int32", "param:x")};
        }});
        bad.push_back({"field-tuple-type", [](auto& p) {
            fn(p, "make").blocks[0].insts[0] = LirObjectInst{false, "%t0", "Tuple[Bool]", "stack", "slot:t0"};
        }});
        bad.push_back({"pair-tuple", [](auto& p) {
            fn(p, "make").blocks[0].insts[0] = LirObjectInst{false, "%t0", "Tuple[Int32,Int32]", "stack", "slot:t0"};
        }});
        bad.push_back({"nested-tuple", [](auto& p) {
            fn(p, "make").blocks[0].insts[0] = LirObjectInst{false, "%t0", "Tuple[Tuple[Int32]]", "stack", "slot:t0"};
        }});
        bad.push_back({"generic-tuple", [](auto& p) {
            fn(p, "make").blocks[0].insts[0] = LirObjectInst{false, "%t0", "Tuple[T]", "stack", "slot:t0"};
        }});
        bad.push_back({"scalar-tuple-abi", [](auto& p) { fn(p, "make").returnPassKind = AbiPassKind::Scalar; }});
        bad.push_back({"wrong-return-abi", [](auto& p) { fn(p, "take").returnPassKind = AbiPassKind::Direct; }});
        bad.push_back({"wrong-param-abi", [](auto& p) { fn(p, "take").params[0].passKind = AbiPassKind::Scalar; }});
        bad.push_back({"unknown-storage", [](auto& p) {
            fn(p, "make").blocks[0].insts[0] = LirObjectInst{false, "%t0", "Tuple[Int32]", "heap", "slot:t0"};
        }});
        bad.push_back({"missing-storage", [](auto& p) { fn(p, "make").blocks[0].insts.erase(fn(p, "make").blocks[0].insts.begin()); }});
        bad.push_back({"duplicate-storage", [](auto& p) {
            fn(p, "make").blocks[0].insts.insert(fn(p, "make").blocks[0].insts.begin() + 1, object("%other", "slot:t0"));
        }});
        bad.push_back({"uninitialized-alloca-read", [](auto& p) {
            auto& main = fn(p, "main");
            main.blocks[0].insts.insert(main.blocks[0].insts.begin() + 1, object("%unused", "slot:unused"));
            main.blocks[0].insts.insert(main.blocks[0].insts.begin() + 2, field("%bad", "%unused", "Tuple[Int32]", "0", "slot:unused"));
        }});
        bad.push_back({"uninitialized-field-read", [](auto& p) {
            fn(p, "take").blocks[0].insts[0] = field("%f", "missing", "Tuple[Int32]", "0", "missing");
        }});
        bad.push_back({"whole-uninitialized", [](auto& p) {
            fn(p, "make").blocks[0].insts[1] = LirReturnInst{keyed("missing", "Tuple[Int32]", "missing")};
        }});
        bad.push_back({"store-param", [](auto& p) {
            auto& take = fn(p, "take");
            take.blocks[0].insts.insert(take.blocks[0].insts.begin(), LirStoreInst{"p", value("1", "Int32"), "param:p"});
        }});
        bad.push_back({"field-store-param", [](auto& p) {
            auto& take = fn(p, "take");
            take.blocks[0].insts.insert(take.blocks[0].insts.begin(), LirStoreFieldInst{
                keyed("p", "Tuple[Int32]", "param:p"), "0", value("1", "Int32")});
        }});
        bad.push_back({"field-store-rvalue", [](auto& p) {
            auto& make = fn(p, "make");
            make.blocks[0].insts[1] = LirStoreFieldInst{value("tuple", "Tuple[Int32]"), "0", keyed("x", "Int32", "param:x")};
        }});
        bad.push_back({"non-proven-safe-field-store", [](auto& p) {
            auto& make = fn(p, "make");
            make.blocks[0].insts[1] = LirStoreFieldInst{keyed("%t0", "Tuple[Int32]", "slot:t0"), "0", keyed("x", "Int32", "param:x"), LirSafetyTag::BoundsCheckRequired};
        }});
        bad.push_back({"wrong-field-result-type", [](auto& p) {
            auto& take = fn(p, "take");
            auto projected = field("%f", "p", "Tuple[Int32]", "0", "param:p");
            projected.type = "Bool";
            take.blocks[0].insts[0] = projected;
        }});
        bad.push_back({"object-is-unit", [](auto& p) {
            fn(p, "make").blocks[0].insts[0] = LirObjectInst{false, "%t0", "Unit", "stack", "slot:t0"};
        }});
        bad.push_back({"value-is-unit", [](auto& p) {
            auto& make = fn(p, "make");
            make.blocks[0].insts[1] = LirStoreFieldInst{keyed("%t0", "Tuple[Int32]", "slot:t0"), "0", value("unit", "Unit", {}, true)};
        }});
        bad.push_back({"wrong-slot-read-type", [](auto& p) {
            auto& main = fn(p, "main");
            main.blocks[0].insts[2] = LirStoreInst{"p", keyed("%made", "Int32", "%made"), "slot:p"};
        }});
        bad.push_back({"duplicate-ssa", [](auto& p) {
            fn(p, "take").blocks[0].insts.insert(fn(p, "take").blocks[0].insts.begin() + 1, field("%f", "p", "Tuple[Int32]", "0", "param:p"));
        }});
        bad.push_back({"parameter-ssa-collision", [](auto& p) {
            fn(p, "take").blocks[0].insts[0] = field("param:p", "p", "Tuple[Int32]", "0", "param:p");
        }});
        bad.push_back({"parameter-slot-collision", [](auto& p) {
            fn(p, "make").blocks[0].insts[0] = object("%x", "param:x");
        }});
        bad.push_back({"call-arity", [](auto& p) { std::get<LirCallInst>(fn(p, "main").blocks[0].insts[1]).args.clear(); }});
        bad.push_back({"call-arg-type", [](auto& p) { std::get<LirCallInst>(fn(p, "main").blocks[0].insts[1]).args[0] = value("true", "Bool"); }});
        bad.push_back({"call-return-type", [](auto& p) { std::get<LirCallInst>(fn(p, "main").blocks[0].insts[4]).type = "Bool"; }});
        bad.push_back({"unknown-target", [](auto& p) { std::get<LirCallInst>(fn(p, "main").blocks[0].insts[1]).callee = "nope"; }});
        bad.push_back({"external-call", [](auto& p) { std::get<LirCallInst>(fn(p, "main").blocks[0].insts[1]).kind = LirCallKind::External; }});
        bad.push_back({"foreign-call", [](auto& p) { std::get<LirCallInst>(fn(p, "main").blocks[0].insts[1]).kind = LirCallKind::Foreign; }});
        bad.push_back({"literal-invalid", [](auto& p) { fn(p, "make").blocks[0].insts[1] = LirStoreFieldInst{keyed("%t0", "Tuple[Int32]", "slot:t0"), "0", value("not-an-int", "Int32")}; }});
        bad.push_back({"literal-out-of-range", [](auto& p) { fn(p, "make").blocks[0].insts[1] = LirStoreFieldInst{keyed("%t0", "Tuple[Int32]", "slot:t0"), "0", value("2147483648", "Int32")}; }});
        bad.push_back({"unresolved-keyed-constant", [](auto& p) { fn(p, "make").blocks[0].insts[1] = LirStoreFieldInst{keyed("%t0", "Tuple[Int32]", "slot:t0"), "0", keyed("42", "Int32", "missing:key")}; }});
        bad.push_back({"drop", [](auto& p) { fn(p, "make").blocks[0].insts.insert(fn(p, "make").blocks[0].insts.begin() + 1, LirDropInst{"x", "Int32", "param:x", {}}); }});
        bad.push_back({"raw-region", [](auto& p) { fn(p, "make").blocks[0].rawRegion = "unsafe { raw }"; }});
        bad.push_back({"bad-linkage", [](auto& p) { fn(p, "make").linkage.abi = "foreign"; }});
        bad.push_back({"bad-link-symbol", [](auto& p) { fn(p, "make").linkage.symbol = "other::make"; }});
        bad.push_back({"cross-function-key", [](auto& p) {
            fn(p, "make").blocks[0].insts[1] = LirStoreFieldInst{
                keyed("%made", "Tuple[Int32]", "%made"), "0", keyed("x", "Int32", "param:x")};
        }});
        bad.push_back({"generic", [](auto& p) { fn(p, "make").returnType = "T"; }});
        bad.push_back({"wrong-whole-tuple-return", [](auto& p) { fn(p, "make").blocks[0].insts.back() = LirReturnInst{keyed("x", "Int32", "param:x")}; }});
        bad.push_back({"return-not-last", [](auto& p) { fn(p, "make").blocks[0].insts.push_back(LirStoreFieldInst{keyed("%t0", "Tuple[Int32]", "slot:t0"), "0", keyed("x", "Int32", "param:x")}); }});
        bad.push_back({"after-terminator", [](auto& p) { fn(p, "make").blocks[0].insts.push_back(LirReturnInst{keyed("%t0", "Tuple[Int32]", "slot:t0")}); }});
        bad.push_back({"bad-successor", [](auto& p) {
            auto chain = validChain();
            fn(p, "main") = fn(chain, "main");
            fn(p, "main").blocks[0].insts.back() = LirGotoInst{"missing"};
        }});
        bad.push_back({"bad-predecessor", [](auto& p) { auto chain = validChain(); fn(p, "main") = fn(chain, "main"); fn(p, "main").blocks[1].predecessors = {"wrong"}; }});
        bad.push_back({"cycle", [](auto& p) { auto chain = validChain(); fn(p, "main") = fn(chain, "main"); fn(p, "main").blocks[1].insts.insert(fn(p, "main").blocks[1].insts.begin(), LirGotoInst{"entry"}); }});
        bad.push_back({"duplicate-label", [](auto& p) { auto chain = validChain(); fn(p, "main") = fn(chain, "main"); fn(p, "main").blocks[1].label = "entry"; }});
        bad.push_back({"branch", [](auto& p) { auto chain = validChain(); fn(p, "main") = fn(chain, "main"); fn(p, "main").blocks[0].insts.back() = LirBranchInst{value("true", "Bool"), "finish", "finish"}; }});
        bad.push_back({"unknown-instruction", [](auto& p) { fn(p, "make").blocks[0].insts[0] = LirDefectInst{"mystery", ""}; }});
        bad.push_back({"phi", [](auto& p) { fn(p, "main").blocks[0].insts.insert(fn(p, "main").blocks[0].insts.begin(), LirPhiInst{"%phi", "Int32", {}}); }});

        bad.push_back({"operand-provenance", [](auto& p) {
            std::get<LirStoreFieldInst>(fn(p,"make").blocks[0].insts[1]).value.provenance = Provenance::forRoot("param:x",1).project(0,2);
        }});
        bad.push_back({"object-provenance", [](auto& p) {
            std::get<LirStoreFieldInst>(fn(p,"make").blocks[0].insts[1]).object.provenance = Provenance::forRoot("slot:t0",1);
        }});
        bad.push_back({"field-provenance", [](auto& p) {
            std::get<LirFieldInst>(fn(p,"take").blocks[0].insts[0]).provenance = Provenance::forRoot("param:p",1).project(0,2);
        }});
        bad.push_back({"malformed-operand-provenance", [](auto& p) {
            std::get<LirReturnInst>(fn(p,"make").blocks[0].insts.back()).value.provenance.source = SourceProjection{"",{0}};
        }});
        bad.push_back({"malformed-field-provenance", [](auto& p) {
            std::get<LirFieldInst>(fn(p,"take").blocks[0].insts[0]).provenance.syntheticStorageId = 0;
        }});
        bad.push_back({"unknown-source-provenance", [](auto& p) {
            std::get<LirFieldInst>(fn(p,"take").blocks[0].insts[0]).provenance = Provenance::forRoot("",7);
        }});
        bad.push_back({"constructor-object-unit-flag", [](auto& p) {
            std::get<LirStoreFieldInst>(fn(p,"make").blocks[0].insts[1]).object.isUnit = true;
        }});
        bad.push_back({"constructor-value-unit-flag", [](auto& p) {
            std::get<LirStoreFieldInst>(fn(p,"make").blocks[0].insts[1]).value.isUnit = true;
        }});
        bad.push_back({"mutable-tuple", [](auto& p) { std::get<LirObjectInst>(fn(p,"make").blocks[0].insts[0]).isMutable = true; }});
        bad.push_back({"second-field-initialization", [](auto& p) {
            auto& list=fn(p,"make").blocks[0].insts; auto instruction=list[1]; list.insert(list.begin()+2,instruction);
        }});
        bad.push_back({"field-write-call-result", [](auto& p) {
            auto& list=fn(p,"main").blocks[0].insts;
            list.insert(list.begin()+2,LirStoreFieldInst{keyed("%made","Tuple[Int32]","%made"),"0",value("42","Int32")});
        }});
        bad.push_back({"wrong-projection-safety", [](auto& p) { std::get<LirFieldInst>(fn(p,"take").blocks[0].insts[0]).safety=LirSafetyTag::UnsafeBoundary; }});
        bad.push_back({"foreign-stable-abi", [](auto& p) { fn(p,"make").linkage.ffiStable=true; }});
        bad.push_back({"shared-linkage", [](auto& p) { fn(p,"make").linkage.linkage=LinkageKind::Shared; }});
        bad.push_back({"call-hook", [](auto& p) { std::get<LirCallInst>(fn(p,"main").blocks[0].insts[1]).hook="foreign.make"; }});
        bad.push_back({"call-safety", [](auto& p) { std::get<LirCallInst>(fn(p,"main").blocks[0].insts[1]).safety=LirSafetyTag::UnsafeBoundary; }});

        std::size_t rejected = 0;
        for (const auto& [name, mutate] : bad) {
            reject(root, name, mutate, log);
            ++rejected;
        }
        const auto allOldFlags = std::array<bool, 3>{{true, true, true}};
        for (const auto& [name, mutate] : bad) {
            auto program = valid();
            mutate(program);
            rejectOne(root, name + "-all-old-flags", program,
                      options(root / (name + "-all-old-flags"), true, allOldFlags[0], allOldFlags[1], allOldFlags[2]), log);
        }

        std::ofstream count(root / "admission.count", std::ios::trunc);
        count << rejected << "\n";
        std::cout << "PASS singleton admission: keyed/source-shaped/unit/goto positives, default and old-flag rejections, "
                  << rejected << " negative mutations (each also checked with all old flags); binary preservation verified\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
