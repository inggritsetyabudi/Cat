#include "codegen/native_object.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
std::string read(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) return {};
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), {});
}
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void rejects(const std::string& ir, const std::filesystem::path& output,
             const std::string& diagnostic) {
    const auto previous = read(output);
    bool rejected = false;
    try {
        claw::codegen::emitNativeObject(ir, output, false);
    } catch (const std::exception& error) {
        rejected = true;
        require(std::string(error.what()).find(diagnostic) != std::string::npos,
                "wrong diagnostic: " + std::string(error.what()));
    }
    require(rejected, "invalid native module was accepted");
    require(read(output) == previous, "failed emission changed the prior object");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "usage: native-object-tests <artifact-directory>");
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        const auto output = directory / "verified.cat.o";
        const std::string target = "target triple = \"x86_64-unknown-linux-gnu\"\n";
        const std::string valid = target + "define i32 @main() { ret i32 7 }\n"
            "define i8 @byte_value() { ret i8 255 }\n";
        for (bool optimize : {false, true}) {
            const auto ir = claw::codegen::emitNativeObject(valid, output, optimize);
            const auto bytes = read(output);
            require(bytes.size() > 4 && bytes.substr(0, 4) == std::string("\177ELF", 4), "not an ELF object");
            require(ir.find("target datalayout") != std::string::npos, "missing target DataLayout");
            require(ir.find("ret i8 255") != std::string::npos, "Cat unsigned IR spelling changed");
            require(ir.find("target triple = \"x86_64-unknown-linux-gnu\"") != std::string::npos,
                    "wrong native target");
        }
        rejects("not LLVM IR", output, "Cannot parse Cat native LLVM IR");
        rejects("target triple = \"aarch64-unknown-linux-gnu\"\ndefine i32 @main() { ret i32 0 }\n",
                output, "target must match");
        rejects("define i32 @main() { ret i32 0 }\n", output, "target must match");
        rejects(target + "target datalayout = \"e-p:32:32\"\ndefine i32 @main() { ret i32 0 }\n",
                output, "DataLayout is incompatible");
        rejects(target + "define i32 @main(i1 %flag) {\nentry:\n br i1 %flag, label %a, label %b\n"
                "a:\n %value = add i32 1, 2\n br label %b\nb:\n ret i32 %value\n}\n",
                output, "verification failed");
        // Existing directory cannot be replaced by an object; staging must clean up.
        const auto blocked = directory / "blocked.cat.o";
        std::filesystem::create_directories(blocked);
        rejects(valid, blocked, "Cannot publish Cat native object");
        require(std::filesystem::is_directory(blocked), "failed publication replaced a directory");
        rejects(valid, directory / "absent-parent" / "module.o", "Cannot stage Cat native object");
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            require(entry.path().filename().string().find(".tmp-") == std::string::npos,
                    "failed native emission leaked a temporary object");
        }
        std::cout << "[native-object-api/pass] O0/O2 ELF, target/layout, parser/verifier, prior-object preservation and staging cleanup\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[native-object-api/fail] " << error.what() << '\n';
        return 1;
    }
}
