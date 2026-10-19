#include "downloadthread.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <regex>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
#endif

namespace {
constexpr std::string_view TitleOutputPrefix = "__YT2OGG_TITLE__";
constexpr std::string_view FileOutputPrefix = "__YT2OGG_FILE__";

std::string pathToUtf8(const std::filesystem::path &path) {
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char *>(encoded.data()), encoded.size());
}

std::filesystem::path pathFromUtf8(std::string_view value) {
#ifdef _WIN32
    std::u8string encoded;
    encoded.reserve(value.size());
    for (unsigned char character : value) encoded.push_back(static_cast<char8_t>(character));
    return std::filesystem::path(encoded);
#else
    return std::filesystem::path(value);
#endif
}

std::int64_t bytesFromUnit(double value, const std::string &unit) {
    const double multiplier = unit.find('G') != std::string::npos ? 1073741824.0 :
        unit.find('M') != std::string::npos ? 1048576.0 :
        unit.find('K') != std::string::npos ? 1024.0 : 1.0;
    return static_cast<std::int64_t>(value * multiplier);
}

#ifdef _WIN32
std::wstring toWide(const std::string &value) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
    result.resize(static_cast<size_t>(size - 1));
    return result;
}

std::string quoteWindowsArgument(const std::wstring &argument) {
    std::wstring quoted = L"\"";
    size_t slashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') {
            ++slashes;
        } else if (character == L'"') {
            quoted.append(slashes * 2 + 1, L'\\');
            quoted.push_back(character);
            slashes = 0;
        } else {
            quoted.append(slashes, L'\\');
            slashes = 0;
            quoted.push_back(character);
        }
    }
    quoted.append(slashes * 2, L'\\');
    quoted.push_back(L'"');
    const int size = WideCharToMultiByte(CP_UTF8, 0, quoted.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, quoted.c_str(), -1, result.data(), size, nullptr, nullptr);
    result.resize(static_cast<size_t>(size - 1));
    return result;
}

bool runProcess(const std::vector<std::string> &arguments, std::atomic_bool &cancelled,
                const std::function<void(const std::string &)> &on_line, int &exit_code) {
    std::wstring command_line;
    for (const std::string &argument : arguments) {
        if (!command_line.empty()) command_line.push_back(L' ');
        command_line += toWide(quoteWindowsArgument(toWide(argument)));
    }

    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) return false;
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const std::wstring executable = toWide(arguments.front());
    const BOOL started = CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr,
                                        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(write_pipe);
    if (!started) {
        CloseHandle(read_pipe);
        return false;
    }

    std::string buffered;
    std::array<char, 4096> bytes{};
    const auto append_bytes = [&buffered, &on_line](const char *data, size_t size) {
        buffered.append(data, size);
        size_t newline;
        while ((newline = buffered.find('\n')) != std::string::npos) {
            on_line(buffered.substr(0, newline));
            buffered.erase(0, newline + 1);
        }
    };
    bool pipe_open = true;
    while (pipe_open) {
        if (cancelled.load()) {
            TerminateProcess(process.hProcess, 1);
        }
        DWORD available = 0;
        if (!PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr)) {
            pipe_open = false;
        } else if (available > 0) {
            DWORD read = 0;
            if (ReadFile(read_pipe, bytes.data(),
                         std::min<DWORD>(available, static_cast<DWORD>(bytes.size())),
                         &read, nullptr) && read > 0) {
                append_bytes(bytes.data(), read);
            }
        } else if (WaitForSingleObject(process.hProcess, 50) == WAIT_OBJECT_0) {
            DWORD read = 0;
            if (!ReadFile(read_pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read == 0) {
                pipe_open = false;
            } else {
                append_bytes(bytes.data(), read);
            }
        } else {
            Sleep(50);
        }
    }
    if (!buffered.empty()) on_line(buffered);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    exit_code = static_cast<int>(code);
    CloseHandle(read_pipe);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}
#else
bool runProcess(const std::vector<std::string> &arguments, std::atomic_bool &cancelled,
                const std::function<void(const std::string &)> &on_line, int &exit_code) {
    int output_pipe[2];
    if (pipe(output_pipe) != 0) return false;
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        return false;
    }
    posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, output_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, output_pipe[1]);
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (const std::string &argument : arguments) {
        argv.push_back(const_cast<char *>(argument.c_str()));
    }
    argv.push_back(nullptr);
    pid_t child = -1;
    const int spawn_status = posix_spawnp(&child, argv.front(), &actions, nullptr,
                                          argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawn_status != 0) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        return false;
    }

    close(output_pipe[1]);
    const int flags = fcntl(output_pipe[0], F_GETFL, 0);
    if (flags >= 0) fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);
    std::string buffered;
    std::array<char, 4096> bytes{};
    const auto append_bytes = [&buffered, &on_line](const char *data, size_t size) {
        buffered.append(data, size);
        size_t newline;
        while ((newline = buffered.find('\n')) != std::string::npos) {
            on_line(buffered.substr(0, newline));
            buffered.erase(0, newline + 1);
        }
    };
    bool child_exited = false;
    bool sent_termination = false;
    auto termination_time = std::chrono::steady_clock::time_point{};

    while (!child_exited) {
        if (cancelled.load() && !sent_termination) {
            kill(child, SIGTERM);
            sent_termination = true;
            termination_time = std::chrono::steady_clock::now();
        }
        if (sent_termination &&
            std::chrono::steady_clock::now() - termination_time > std::chrono::seconds(2)) {
            kill(child, SIGKILL);
        }

        pollfd descriptor{output_pipe[0], POLLIN | POLLHUP, 0};
        poll(&descriptor, 1, 100);
        if (descriptor.revents & (POLLIN | POLLHUP)) {
            ssize_t count;
            while ((count = read(output_pipe[0], bytes.data(), bytes.size())) > 0) {
                append_bytes(bytes.data(), static_cast<size_t>(count));
            }
        }
        int status = 0;
        if (waitpid(child, &status, WNOHANG) == child) {
            child_exited = true;
            exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
    }
    ssize_t count;
    while ((count = read(output_pipe[0], bytes.data(), bytes.size())) > 0) {
        append_bytes(bytes.data(), static_cast<size_t>(count));
    }
    if (!buffered.empty()) on_line(buffered);
    close(output_pipe[0]);
    return true;
}
#endif
}

DownloadThread::DownloadThread(std::string url, std::filesystem::path output_directory,
                               std::filesystem::path ytdlp_path, int quality,
                               Callbacks callbacks)
    : url_(std::move(url)),
      output_directory_(std::move(output_directory)),
      ytdlp_path_(std::move(ytdlp_path)),
      quality_(std::clamp(quality, 0, 3)),
      callbacks_(std::move(callbacks)) {}

DownloadThread::~DownloadThread() {
    cancel();
    join();
}

void DownloadThread::start() {
    thread_ = std::thread(&DownloadThread::run, this);
}

void DownloadThread::cancel() {
    cancelled_.store(true);
}

void DownloadThread::join() {
    if (thread_.joinable()) thread_.join();
}

void DownloadThread::run() {
    std::vector<std::string> arguments{
        pathToUtf8(ytdlp_path_),
        "--no-playlist", "--newline", "--progress", "--no-simulate", "--color", "never",
        "--print", "after_move:__YT2OGG_FILE__%(filepath)s",
        "--print", "before_dl:__YT2OGG_TITLE__%(title)s",
        "--no-quiet", "-f", "bestaudio[acodec=opus]", "-x",
        "--audio-format", "opus"
    };
    constexpr std::array<const char *, 3> quality_bitrates = {"96K", "80K", "64K"};
    if (quality_ > 0) {
        const std::string bitrate = quality_bitrates[quality_ - 1];
        arguments.insert(arguments.end(), {
            "--audio-quality", bitrate, "--postprocessor-args",
            "ExtractAudio+ffmpeg_o:-c:a libopus -b:a " + bitrate
        });
    }
    arguments.insert(arguments.end(), {
        "-o", pathToUtf8(output_directory_ / "%(title)s.%(ext)s"), "--", url_
    });

    int exit_code = -1;
    const bool started = runProcess(arguments, cancelled_,
                                    [this](const std::string &line) { processLine(line); },
                                    exit_code);
    if (!started) {
        finish(-1);
        return;
    }
    if (cancelled_.load()) {
        finish(-2);
        return;
    }
    if (exit_code != 0) {
        finish(-3);
        return;
    }

    std::error_code error;
    if (output_file_path_.empty() || !std::filesystem::is_regular_file(output_file_path_, error)) {
        finish(-4);
        return;
    }

    if (output_file_path_.extension() == ".ogg") {
        finish(0, output_file_path_);
        return;
    }

    std::filesystem::path ogg_path = output_file_path_;
    ogg_path.replace_extension(".ogg");
    const auto output_stem = output_file_path_.parent_path() / output_file_path_.stem();
    if (std::filesystem::exists(ogg_path, error)) {
        int suffix = 1;
        do {
            ogg_path = output_stem;
            ogg_path += " (" + std::to_string(suffix++) + ").ogg";
            error.clear();
        } while (std::filesystem::exists(ogg_path, error));
    }
    error.clear();
    std::filesystem::rename(output_file_path_, ogg_path, error);
    if (error) {
        finish(-4);
        return;
    }
    finish(0, ogg_path);
}

void DownloadThread::processLine(const std::string &line) {
    if (!line.empty() && line.back() == '\r') {
        processLine(line.substr(0, line.size() - 1));
        return;
    }
    if (parseProgressLine(line)) return;
    if (line.starts_with(TitleOutputPrefix)) {
        if (callbacks_.title_found) callbacks_.title_found(line.substr(TitleOutputPrefix.size()));
        return;
    }
    if (line.starts_with(FileOutputPrefix)) {
        const auto printed_file = pathFromUtf8(line.substr(FileOutputPrefix.size()));
        const auto resolved_path = printed_file.is_absolute()
            ? printed_file : output_directory_ / printed_file;
        output_file_path_ = std::filesystem::absolute(resolved_path);
        return;
    }
    std::string lowercase = line;
    std::transform(lowercase.begin(), lowercase.end(), lowercase.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (lowercase.find("[extractaudio]") != std::string::npos && callbacks_.status_changed) {
        callbacks_.status_changed("Encoding");
    }

}

bool DownloadThread::parseProgressLine(const std::string &line) {
    static const std::regex progress_regex(
        R"(\[download\]\s+(\d+\.?\d*)%\s+of\s+([\d.]+)([KMGT]?i?B?)\s+at\s+([\d.]+)([KMGT]?i?B?/s)\s+ETA\s+(?:\d+:\d+|0:00))");
    std::smatch match;
    if (!std::regex_search(line, match, progress_regex)) return false;

    const double percent = std::stod(match[1].str());
    const auto total = bytesFromUnit(std::stod(match[2].str()), match[3].str());
    const auto speed = bytesFromUnit(std::stod(match[4].str()), match[5].str());
    if (callbacks_.progress) {
        callbacks_.progress(static_cast<std::int64_t>(static_cast<double>(total) * percent / 100.0),
                            total);
    }
    if (callbacks_.speed_changed) callbacks_.speed_changed(speed);
    return true;
}

void DownloadThread::finish(int status, const std::filesystem::path &path) {
    if (callbacks_.finished) callbacks_.finished(status, path);
}
