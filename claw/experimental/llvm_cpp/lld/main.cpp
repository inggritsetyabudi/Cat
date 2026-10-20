// Isolated linker worker, never part of claw's production driver.
#include <lld/Common/Driver.h>
#include <lld/Common/ErrorHandler.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/raw_ostream.h>
#include <array>
#include <cstring>
#include <vector>

#if LLVM_VERSION_MAJOR != 23 || LLVM_VERSION_MINOR != 1 || LLVM_VERSION_PATCH != 3
#error The isolated LLD worker requires exact LLVM 23.1.3 headers
#endif

LLD_HAS_DRIVER(elf)
LLD_HAS_DRIVER(coff)
LLD_HAS_DRIVER(mingw)

namespace {
const std::array<lld::DriverDef, 3> drivers{{
    {lld::Gnu, &lld::elf::link},
    {lld::MinGW, &lld::mingw::link},
    {lld::WinLink, &lld::coff::link},
}};

// Every normal worker process makes exactly one serial call. No concurrent API
// calls, compiler-process embedding, or recovery from untrusted objects is claimed.
lld::Result invoke(llvm::ArrayRef<const char*> args,
                   llvm::raw_ostream& out, llvm::raw_ostream& err) {
    const auto result = lld::lldMain(args, out, err, drivers);
    out.flush();
    err.flush();
    if (!result.canRunAgain)
        lld::exitLld(result.retCode ? result.retCode : 1);
    return result;
}
}

int main(int argc, char** argv) {
    llvm::InitLLVM init(argc, argv);
    if (argc == 2 && std::strcmp(argv[1], "--sdk-version") == 0) {
        llvm::cl::PrintVersionMessage();
        llvm::outs() << "LLVM_VERSION_STRING=" << LLVM_VERSION_STRING << '\n'
                     << "LLD_LIBRARY=";
        llvm::outs().flush();
        const char* version[]{"ld.lld", "--version"};
        return invoke(version, llvm::outs(), llvm::errs()).retCode;
    }
    if (argc == 2 && std::strcmp(argv[1], "--poc-reentry-check") == 0) {
        // Safe option-error recovery, not a deliberate corrupted-object crash.
        llvm::raw_null_ostream sink;
        const char* good[]{"ld.lld", "--version"};
        const char* bad[]{"ld.lld", "--cat-poc-deliberately-unknown-option"};
        const auto first = invoke(good, sink, sink);
        const auto failed = invoke(bad, sink, sink);
        const auto last = invoke(good, sink, sink);
        if (first.retCode || !failed.retCode || last.retCode)
            return 1;
        llvm::outs() << "serial reentry: success / expected error / success; canRunAgain=true\n";
        return 0;
    }
    std::vector<const char*> args;
    args.reserve(static_cast<std::size_t>(argc));
    args.push_back("ld.lld"); // stable GNU dispatch; -m i386pep selects MinGW.
    for (int i = 1; i < argc; ++i)
        args.push_back(argv[i]);
    return invoke(args, llvm::outs(), llvm::errs()).retCode;
}
