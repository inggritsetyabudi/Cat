#include "driver/native_build.h"

#include "codegen/llvm.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace {

std::string sanitizeStem(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (const char c : raw) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out.push_back(c);
        } else if (out.empty() || out.back() != '_') {
            out.push_back('_');
        }
    }
    return out.empty() ? std::string("main") : out;
}

#if defined(_WIN32)
std::wstring widen(const std::filesystem::path& path) {
    return path.wstring();
}
#endif

std::filesystem::path makeTempLlPath() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
#if defined(_WIN32)
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    for (int attempt = 0; attempt < 256; ++attempt) {
        const auto candidate = dir / ("claw-native-" + std::to_string(pid) + "-" + std::to_string(attempt) + ".ll");
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("Unable to allocate a temporary LLVM IR path for native build.");
}

#if !defined(_WIN32)
class TemporaryClangStderrFile {
public:
    static TemporaryClangStderrFile create() {
        const std::filesystem::path dir = std::filesystem::temp_directory_path();
        const std::string nameTemplate = (dir / "claw-clang-XXXXXX").string();
        std::vector<char> pathTemplate(nameTemplate.begin(), nameTemplate.end());
        pathTemplate.push_back('\0');
        const int fd = mkstemp(pathTemplate.data());
        if (fd == -1) {
            throw std::runtime_error("Unable to create a temporary clang diagnostics file for native build.");
        }

        int diagnosticsFd = fd;
        if (diagnosticsFd == STDERR_FILENO) {
            diagnosticsFd = fcntl(fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
            if (diagnosticsFd == -1) {
                const int duplicateError = errno;
                close(fd);
                unlink(pathTemplate.data());
                throw std::runtime_error(
                    "Unable to reserve a descriptor for temporary clang diagnostics (" +
                    std::string(std::strerror(duplicateError)) + ").");
            }
            if (close(fd) != 0) {
                const int closeError = errno;
                close(diagnosticsFd);
                unlink(pathTemplate.data());
                throw std::runtime_error(
                    "Unable to release the temporary clang diagnostics descriptor (" +
                    std::string(std::strerror(closeError)) + ").");
            }
        }

        try {
            return TemporaryClangStderrFile(std::filesystem::path(pathTemplate.data()), diagnosticsFd);
        } catch (...) {
            const int savedErrno = errno;
            close(diagnosticsFd);
            unlink(pathTemplate.data());
            errno = savedErrno;
            throw;
        }
    }

    TemporaryClangStderrFile(const TemporaryClangStderrFile&) = delete;
    TemporaryClangStderrFile& operator=(const TemporaryClangStderrFile&) = delete;

    ~TemporaryClangStderrFile() {
        if (fd_ >= 0) {
            close(fd_);
        }
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    int getDescriptor() const noexcept {
        return fd_;
    }

private:
    TemporaryClangStderrFile(std::filesystem::path path, int fd) noexcept
        : path_(std::move(path)), fd_(fd) {}

    std::filesystem::path path_;
    int fd_;
};

void reportSpawnFileActionsDestroyFailure(int error) noexcept {
    char message[256]{};
    const int count = std::snprintf(
        message,
        sizeof(message),
        "Warning: unable to release clang spawn file actions (%s).\n",
        std::strerror(error));
    if (count <= 0) {
        return;
    }

    const std::size_t length =
        count >= static_cast<int>(sizeof(message)) ? sizeof(message) - 1 : static_cast<std::size_t>(count);
    std::size_t written = 0;
    while (written < length) {
        const ssize_t result = write(STDERR_FILENO, message + written, length - written);
        if (result > 0) {
            written += static_cast<std::size_t>(result);
        } else if (result == -1 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

class ScopedSpawnFileActions {
public:
    explicit ScopedSpawnFileActions(posix_spawn_file_actions_t& actions) noexcept : actions_(&actions) {}
    ScopedSpawnFileActions(const ScopedSpawnFileActions&) = delete;
    ScopedSpawnFileActions& operator=(const ScopedSpawnFileActions&) = delete;

    ~ScopedSpawnFileActions() {
        if (initialized_) {
            const int error = destroy();
            if (error != 0) {
                reportSpawnFileActionsDestroyFailure(error);
            }
        }
    }

    int destroy() noexcept {
        if (!initialized_) {
            return 0;
        }
        initialized_ = false;
        return posix_spawn_file_actions_destroy(actions_);
    }

private:
    posix_spawn_file_actions_t* actions_;
    bool initialized_ = true;
};

std::string spawnFileActionsDestroyNote(int error) {
    if (error == 0) {
        return {};
    }
    return " Additionally, releasing clang spawn file actions failed (" + std::string(std::strerror(error)) + ").";
}

void forwardClangDiagnostics(int diagnosticsFd) {
    std::array<char, 8192> chunk{};
    off_t offset = 0;
    for (;;) {
        const ssize_t count = pread(diagnosticsFd, chunk.data(), chunk.size(), offset);
        if (count > 0) {
            std::cerr.write(chunk.data(), static_cast<std::streamsize>(count));
            offset += count;
            continue;
        }
        if (count == 0) {
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        throw std::runtime_error(
            "Unable to read the temporary clang diagnostics descriptor (" +
            std::string(std::strerror(errno)) + ").");
    }
}
#endif

void writeTextFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to write temporary file '" + path.string() + "'.");
    }
    out << content;
    if (!out.good()) {
        throw std::runtime_error("Failed to flush temporary file '" + path.string() + "'.");
    }
}

std::filesystem::path locateRuntimeSource(const std::filesystem::path& compilerBinaryPath) {
    const auto normalizedCompiler = std::filesystem::absolute(compilerBinaryPath);
    const auto exeDir = normalizedCompiler.parent_path();
    const std::vector<std::filesystem::path> candidates = {
        exeDir / "runtime" / "native_runtime.c",
        exeDir.parent_path() / "runtime" / "native_runtime.c",
        exeDir.parent_path().parent_path() / "runtime" / "native_runtime.c",
        std::filesystem::current_path() / "runtime" / "native_runtime.c",
    };

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::absolute(candidate);
        }
    }

    throw std::runtime_error(
        "Unable to locate bundled native runtime source 'runtime/native_runtime.c' relative to compiler executable.");
}

std::string llvmArtifactNote(const std::filesystem::path& llvmPath) {
    return " Generated LLVM IR was left at '" + llvmPath.string() + "'.";
}

int runClangLink(
    const std::filesystem::path& llvmPath,
    const std::filesystem::path& runtimePath,
    const std::filesystem::path& outputPath,
    const std::function<void()>& beforeLinkOutput) {
#if defined(_WIN32)
    const std::vector<std::wstring> argsStorage = {
        L"clang",
        L"-O2",
        L"-std=c11",
        widen(llvmPath),
        widen(runtimePath),
        L"-o",
        widen(outputPath),
    };

    std::vector<const wchar_t*> argv;
    argv.reserve(argsStorage.size() + 1);
    for (const auto& arg : argsStorage) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    beforeLinkOutput();
    const int result = _wspawnvp(_P_WAIT, L"clang", argv.data());
    if (result == -1) {
        throw std::runtime_error("Failed to launch `clang` from PATH while building native executable.");
    }
    return result;
#else
    TemporaryClangStderrFile diagnosticsFile = TemporaryClangStderrFile::create();
    const int diagnosticsFd = diagnosticsFile.getDescriptor();

    const std::vector<std::string> argsStorage = {
        "clang",
        "-O2",
        "-std=c11",
        llvmPath.string(),
        runtimePath.string(),
        "-o",
        outputPath.string(),
    };
    std::vector<char*> argv;
    argv.reserve(argsStorage.size() + 1);
    for (const auto& arg : argsStorage) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t fileActions;
    int setupError = posix_spawn_file_actions_init(&fileActions);
    if (setupError != 0) {
        throw std::runtime_error(
            "Unable to prepare stderr capture for `clang` (" + std::string(std::strerror(setupError)) + ")." +
            llvmArtifactNote(llvmPath));
    }
    ScopedSpawnFileActions scopedFileActions(fileActions);
    setupError = posix_spawn_file_actions_adddup2(&fileActions, diagnosticsFd, STDERR_FILENO);
    if (setupError == 0 && diagnosticsFd != STDERR_FILENO) {
        setupError = posix_spawn_file_actions_addclose(&fileActions, diagnosticsFd);
    }
    if (setupError != 0) {
        const int destroyError = scopedFileActions.destroy();
        throw std::runtime_error(
            "Unable to prepare stderr capture for `clang` (" + std::string(std::strerror(setupError)) + ")." +
            spawnFileActionsDestroyNote(destroyError) + llvmArtifactNote(llvmPath));
    }

    pid_t child = -1;
    const int spawnError = posix_spawnp(&child, "clang", &fileActions, nullptr, argv.data(), environ);
    const int destroyError = scopedFileActions.destroy();

    const auto stopAndForwardDiagnostics = [&]() {
        beforeLinkOutput();
        forwardClangDiagnostics(diagnosticsFd);
    };

    if (spawnError != 0) {
        stopAndForwardDiagnostics();
        const std::string destroyNote = spawnFileActionsDestroyNote(destroyError);
        if (spawnError == ENOENT || spawnError == EACCES || spawnError == ENOEXEC) {
            throw std::runtime_error(
                "`clang` could not be found or executed from PATH (posix_spawnp: " +
                std::string(std::strerror(spawnError)) + ")." + destroyNote + llvmArtifactNote(llvmPath));
        }
        throw std::runtime_error(
            "Unable to spawn the `clang` process (posix_spawnp failed: " +
            std::string(std::strerror(spawnError)) + ")." + destroyNote + llvmArtifactNote(llvmPath));
    }

    int status = 0;
    pid_t waited = -1;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited == -1 && errno == EINTR);
    if (waited == -1) {
        const int waitError = errno;
        stopAndForwardDiagnostics();
        throw std::runtime_error(
            "Unable to wait for the `clang` process (waitpid failed: " +
            std::string(std::strerror(waitError)) + ")." + spawnFileActionsDestroyNote(destroyError) +
            llvmArtifactNote(llvmPath));
    }

    stopAndForwardDiagnostics();
    if (destroyError != 0) {
        reportSpawnFileActionsDestroyFailure(destroyError);
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        throw std::runtime_error(
            "`clang` terminated by signal " + std::to_string(WTERMSIG(status)) + "." + llvmArtifactNote(llvmPath));
    }
    throw std::runtime_error("Unable to decode the `clang` process wait status." + llvmArtifactNote(llvmPath));
#endif
}

} // namespace

namespace claw::driver {

std::filesystem::path defaultNativeOutputPath(const claw::workspace::LoadedProject& project) {
    const std::string preferredName =
        project.config.has_value() && !project.config->name.empty()
            ? project.config->name
            : project.units[project.entryIndex].path.stem().string();
    return project.packageRoot / "build" / (sanitizeStem(preferredName) + ".exe");
}

std::filesystem::path buildNativeExecutable(
    const claw::workspace::LoadedProject& project,
    std::string_view entryRealm,
    const std::vector<claw::frontend::OirUnitView>& units,
    const std::filesystem::path& compilerBinaryPath,
    const std::filesystem::path& outputPath,
    const std::function<void()>& beforeLinkOutput) {
    (void)project;
    const auto runtimePath = locateRuntimeSource(compilerBinaryPath);
    const auto absoluteOutput = std::filesystem::absolute(outputPath);
    const auto outputDir = absoluteOutput.parent_path();
    if (!outputDir.empty()) {
        std::filesystem::create_directories(outputDir);
    }

    auto llvmOutputPath = absoluteOutput;
    llvmOutputPath.replace_extension(".ll");
    const std::string llvmIr = claw::codegen::emitNativeLlvmIr(entryRealm, units);
    writeTextFile(llvmOutputPath, llvmIr);

    const int result = runClangLink(llvmOutputPath, runtimePath, absoluteOutput, beforeLinkOutput);
    if (result != 0) {
        throw std::runtime_error(
            "`clang` exited with status " + std::to_string(result) +
            " while building the native executable. Generated LLVM IR was left at '" + llvmOutputPath.string() + "'.");
    }

    return absoluteOutput;
}

} // namespace claw::driver
