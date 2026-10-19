#include "mainwindow.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <string_view>
#include <thread>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#else
#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
constexpr std::size_t MaxConcurrentDownloads = 3;
constexpr std::int64_t SizeThreshold = 1024;

slint::SharedString toSlintString(std::string_view value) {
    return slint::SharedString(value);
}

std::string fromSlintString(const slint::SharedString &value) {
    return std::string(value.data(), value.size());
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

std::string pathToUtf8(const std::filesystem::path &path) {
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char *>(encoded.data()), encoded.size());
}

std::filesystem::path homeDirectory() {
#ifdef _WIN32
    if (const char *profile = std::getenv("USERPROFILE")) return profile;
#else
    if (const char *home = std::getenv("HOME")) return home;
#endif
    return std::filesystem::current_path();
}

std::filesystem::path defaultDownloadDirectory() {
    const auto downloads = homeDirectory() / "Downloads";
    std::error_code error;
    if (std::filesystem::is_directory(downloads, error)) return downloads;
    return homeDirectory();
}

std::optional<std::filesystem::path> chooseOutputDirectory(
    const std::filesystem::path &initial_directory, std::string &error_message) {
#ifdef _WIN32
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        error_message = "Could not initialize the folder picker.";
        return std::nullopt;
    }

    IFileOpenDialog *dialog = nullptr;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&dialog));
    if (FAILED(result)) {
        error_message = "Could not create the folder picker.";
        if (uninitialize) CoUninitialize();
        return std::nullopt;
    }

    FILEOPENDIALOGOPTIONS options{};
    result = dialog->GetOptions(&options);
    if (SUCCEEDED(result)) {
        result = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                    FOS_PATHMUSTEXIST);
    }
    if (SUCCEEDED(result)) {
        dialog->SetTitle(L"Choose output folder");
        IShellItem *initial_folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial_directory.c_str(), nullptr,
                                                  IID_PPV_ARGS(&initial_folder)))) {
            dialog->SetFolder(initial_folder);
            initial_folder->Release();
        }
        result = dialog->Show(nullptr);
    }

    std::optional<std::filesystem::path> selected_directory;
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        // Closing the picker without choosing a folder is not an error.
    } else if (SUCCEEDED(result)) {
        IShellItem *selected_item = nullptr;
        result = dialog->GetResult(&selected_item);
        if (SUCCEEDED(result)) {
            PWSTR selected_path = nullptr;
            result = selected_item->GetDisplayName(SIGDN_FILESYSPATH, &selected_path);
            if (SUCCEEDED(result)) {
                selected_directory.emplace(selected_path);
                CoTaskMemFree(selected_path);
            }
            selected_item->Release();
        }
        if (FAILED(result)) error_message = "Could not read the selected folder.";
    } else {
        error_message = "Could not open the folder picker.";
    }

    dialog->Release();
    if (uninitialize) CoUninitialize();
    return selected_directory;
#else
    struct CommandResult {
        bool started = false;
        int exit_code = -1;
        std::string output;
    };
    const auto run_command = [](const std::vector<std::string> &arguments,
                                CommandResult &command_result, std::string &error) {
        int output_pipe[2];
        if (pipe(output_pipe) != 0) {
            error = "Could not create a folder-picker pipe: " + std::string(std::strerror(errno));
            return;
        }

        posix_spawn_file_actions_t actions;
        int spawn_error = posix_spawn_file_actions_init(&actions);
        if (spawn_error != 0) {
            close(output_pipe[0]);
            close(output_pipe[1]);
            error = "Could not prepare the folder picker: " +
                std::string(std::strerror(spawn_error));
            return;
        }
        if (spawn_error == 0) {
            spawn_error = posix_spawn_file_actions_adddup2(&actions, output_pipe[1],
                                                            STDOUT_FILENO);
        }
        if (spawn_error == 0) {
            spawn_error = posix_spawn_file_actions_addclose(&actions, output_pipe[0]);
        }
        if (spawn_error == 0) {
            spawn_error = posix_spawn_file_actions_addclose(&actions, output_pipe[1]);
        }
        if (spawn_error != 0) {
            posix_spawn_file_actions_destroy(&actions);
            close(output_pipe[0]);
            close(output_pipe[1]);
            error = "Could not prepare the folder picker: " +
                std::string(std::strerror(spawn_error));
            return;
        }

        std::vector<char *> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto &argument : arguments) {
            argv.push_back(const_cast<char *>(argument.c_str()));
        }
        argv.push_back(nullptr);
        pid_t child = -1;
        spawn_error = posix_spawnp(&child, argv.front(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        close(output_pipe[1]);
        if (spawn_error != 0) {
            close(output_pipe[0]);
            if (spawn_error != ENOENT) {
                error = "Could not start the folder picker: " +
                    std::string(std::strerror(spawn_error));
            }
            return;
        }
        command_result.started = true;

        char buffer[1024];
        int read_error = 0;
        for (;;) {
            const ssize_t bytes_read = read(output_pipe[0], buffer, sizeof(buffer));
            if (bytes_read > 0) {
                command_result.output.append(buffer, static_cast<std::size_t>(bytes_read));
            } else if (bytes_read == 0) {
                break;
            } else if (errno != EINTR) {
                read_error = errno;
                break;
            }
        }
        close(output_pipe[0]);

        int child_status = 0;
        while (waitpid(child, &child_status, 0) < 0) {
            if (errno != EINTR) {
                error = "Could not wait for the folder picker to finish: " +
                    std::string(std::strerror(errno));
                return;
            }
        }
        if (read_error != 0) {
            error = "Could not read the folder-picker result: " +
                std::string(std::strerror(read_error));
            return;
        }
        command_result.exit_code = WIFEXITED(child_status) ? WEXITSTATUS(child_status) : -1;
    };

#ifdef __APPLE__
    CommandResult result;
    run_command({"osascript", "-e",
                 "try\n"
                 "set selectedFolder to choose folder with prompt \"Choose output folder\"\n"
                 "POSIX path of selectedFolder\n"
                 "on error number -128\n"
                 "\"__YT2OGG_CANCELLED__\"\n"
                 "end try"},
                result, error_message);
    if (!error_message.empty()) return std::nullopt;
    if (!result.started) {
        error_message = "Could not find osascript to open the folder picker.";
        return std::nullopt;
    }
    if (result.exit_code != 0) {
        error_message = "The folder picker failed.";
        return std::nullopt;
    }
    while (!result.output.empty() &&
           (result.output.back() == '\n' || result.output.back() == '\r')) {
        result.output.pop_back();
    }
    if (result.output == "__YT2OGG_CANCELLED__") return std::nullopt;
#else
    std::string initial_path = pathToUtf8(initial_directory);
    if (!initial_path.empty() && initial_path.back() != '/') initial_path.push_back('/');
    CommandResult result;
    run_command({"zenity", "--file-selection", "--directory",
                 "--title=Choose output folder", "--filename=" + initial_path},
                result, error_message);
    if (!error_message.empty()) return std::nullopt;
    if (!result.started) {
        run_command({"kdialog", "--getexistingdirectory", pathToUtf8(initial_directory),
                     "--title", "Choose output folder"}, result, error_message);
        if (!error_message.empty()) return std::nullopt;
        if (!result.started) {
            error_message = "Install zenity or kdialog to choose an output folder.";
            return std::nullopt;
        }
    }
    if (result.exit_code == 1) return std::nullopt;
#endif

    if (result.exit_code != 0) {
        error_message = "The folder picker failed.";
        return std::nullopt;
    }
    while (!result.output.empty() &&
           (result.output.back() == '\n' || result.output.back() == '\r')) {
        result.output.pop_back();
    }
    if (result.output.empty()) {
        error_message = "The folder picker did not return a folder.";
        return std::nullopt;
    }
    return pathFromUtf8(result.output);
#endif
}

std::filesystem::path settingsFile() {
#ifdef _WIN32
    if (const char *app_data = std::getenv("APPDATA")) {
        return std::filesystem::path(app_data) / "YT2Ogg" / "settings.ini";
    }
#else
    if (const char *config = std::getenv("XDG_CONFIG_HOME")) {
        return std::filesystem::path(config) / "yt2ogg" / "settings.ini";
    }
#endif
    return homeDirectory() / ".config" / "yt2ogg" / "settings.ini";
}

void openWithSystem(const std::filesystem::path &path) {
#ifdef _WIN32
    const std::wstring wide_path = path.wstring();
    ShellExecuteW(nullptr, L"open", wide_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    const pid_t child = fork();
    if (child == 0) {
#ifdef __APPLE__
        execlp("open", "open", path.c_str(), static_cast<char *>(nullptr));
#else
        execlp("xdg-open", "xdg-open", path.c_str(), static_cast<char *>(nullptr));
#endif
        _exit(127);
    }
    if (child > 0) {
        std::thread([child] {
            int status = 0;
            waitpid(child, &status, 0);
        }).detach();
    }
#endif
}

std::filesystem::path executablePath(const char *argv0) {
    std::error_code error;
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) {
        buffer.resize(length);
        return buffer;
    }
#elif defined(__APPLE__)
    uint32_t buffer_size = 0;
    _NSGetExecutablePath(nullptr, &buffer_size);
    std::string buffer(buffer_size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &buffer_size) == 0) {
        return std::filesystem::weakly_canonical(buffer.c_str(), error);
    }
#else
    const auto proc_path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) return proc_path;
#endif
    return std::filesystem::absolute(argv0, error);
}
}

MainWindow::MainWindow(std::filesystem::path executable_path)
    : executable_path_(executablePath(executable_path.string().c_str())),
      settings_path_(settingsFile()),
      window_(MainWindowUI::create()),
      history_model_(std::make_shared<slint::VectorModel<DownloadItem>>()) {
    window_->set_history(history_model_);
    loadSettings();

    window_->on_download_requested([this](const slint::SharedString &url) {
        const std::string value = fromSlintString(url);
        static const std::regex youtube_url(
            R"(^https?://(?:www\.)?(?:youtube\.com/(?:watch\?v=|shorts/)|youtu\.be/)\S+$)",
            std::regex::icase);
        if (!std::regex_match(value, youtube_url)) {
            window_->set_status_text("Enter a valid YouTube URL (youtube.com or youtu.be).");
            return;
        }

        std::error_code error;
        const auto output_directory = getOutputDir();
        if (output_directory.empty() ||
            !std::filesystem::is_directory(output_directory, error)) {
            window_->set_status_text("Choose an existing output folder first.");
            return;
        }

        window_->set_url("");
        download_queue_.push_back(addDownloadItem(value));
        processDownloadQueue();
    });
    window_->on_choose_output_directory([this] {
        std::string picker_error;
        const auto path = chooseOutputDirectory(getOutputDir(), picker_error);
        if (!picker_error.empty()) {
            window_->set_status_text(toSlintString(picker_error));
            return;
        }
        if (!path) return;

        std::error_code error;
        if (!std::filesystem::is_directory(*path, error)) {
            window_->set_status_text("The selected output folder is not accessible.");
            return;
        }
        window_->set_output_directory(toSlintString(pathToUtf8(*path)));
        saveSettings();
        window_->set_status_text("Output folder updated.");
    });
    window_->on_quality_changed([this](int quality) {
        window_->set_quality(std::clamp(quality, 0, 3));
        saveSettings();
    });
    window_->on_open_file([this](int index) { openFile(index); });
    window_->on_open_folder([this](int index) { openFolder(index); });
    window_->on_open_project_link([] {
        openWithSystem(pathFromUtf8("https://github.com/jbm101/YT2Ogg"));
    });
    window_->on_remove_item([this](int index) { removeItem(index); });
    window_->on_clear_history([this] {
        download_history_.erase(
            std::remove_if(download_history_.begin(), download_history_.end(),
                           [](const DownloadInfo &info) { return info.status != 1; }),
            download_history_.end());
        download_queue_.clear();
        refreshHistory();
        updateDownloadButton();
        refreshStatus();
    });

    const auto alive = alive_;
    window_->window().on_close_requested([alive] {
        if (alive->load()) slint::quit_event_loop();
        return slint::CloseRequestResponse::HideWindow;
    });
}

MainWindow::~MainWindow() {
    alive_->store(false);
    for (auto &[download, id] : active_downloads_) {
        (void)id;
        download->cancel();
    }
    for (auto &download : download_workers_) {
        download->join();
    }
}

int MainWindow::run() {
    window_->show();
    window_->run();
    return 0;
}

void MainWindow::loadSettings() {
    std::filesystem::path output_directory;
    int quality = 0;
    {
        std::ifstream settings(settings_path_);
        std::string key;
        while (settings >> key) {
            if (key == "downloadDir") {
                std::string path;
                settings >> std::quoted(path);
                output_directory = pathFromUtf8(path);
            } else if (key == "quality") {
                settings >> quality;
            } else {
                std::string ignored;
                std::getline(settings, ignored);
            }
        }
    }

    std::error_code error;
    if (output_directory.empty() || !std::filesystem::is_directory(output_directory, error)) {
        output_directory = defaultDownloadDirectory();
    }
    window_->set_output_directory(toSlintString(pathToUtf8(output_directory)));
    window_->set_quality(std::clamp(quality, 0, 3));
    saveSettings();
}

void MainWindow::saveSettings() {
    std::error_code error;
    std::filesystem::create_directories(settings_path_.parent_path(), error);
    std::ofstream settings(settings_path_, std::ios::trunc);
    if (!settings) return;
    settings << "downloadDir " << std::quoted(pathToUtf8(getOutputDir())) << '\n'
             << "quality " << getAudioQuality() << '\n';
}

std::filesystem::path MainWindow::getOutputDir() const {
    return pathFromUtf8(fromSlintString(window_->get_output_directory()));
}

int MainWindow::getAudioQuality() const {
    return window_->get_quality();
}

std::uint64_t MainWindow::addDownloadItem(const std::string &url) {
    DownloadInfo info;
    info.id = next_download_id_++;
    info.url = url;
    const auto scheme = url.find("://");
    const auto path_begin = scheme == std::string::npos ? 0 : scheme + 3;
    const auto path_end = url.find('/', path_begin);
    info.title = url.substr(path_begin, path_end == std::string::npos
                                          ? std::string::npos : path_end - path_begin);
    if (path_end != std::string::npos) info.title += url.substr(path_end);
    download_history_.push_back(std::move(info));
    refreshHistory();
    return download_history_.back().id;
}

void MainWindow::startDownload(std::uint64_t download_id) {
    const int index = findDownloadIndex(download_id);
    if (index < 0) return;

    DownloadInfo info = std::move(download_history_[static_cast<std::size_t>(index)]);
    download_history_.erase(download_history_.begin() + index);
    info.status = 1;
    info.start_time = std::chrono::steady_clock::now();
    info.progress = "-";
    info.speed = "-";
    download_history_.insert(download_history_.begin(), std::move(info));
    refreshHistory();

    auto ytdlp_path = executable_path_.parent_path() /
#ifdef _WIN32
        "yt-dlp.exe";
#else
        "yt-dlp";
#endif
    std::error_code error;
    if (!std::filesystem::is_regular_file(ytdlp_path, error)) {
#ifdef _WIN32
        ytdlp_path = "yt-dlp.exe";
#else
        ytdlp_path = "yt-dlp";
#endif
    }

    const auto alive = alive_;
    DownloadThread::Callbacks callbacks;
    callbacks.progress = [this, alive, download_id](std::int64_t received, std::int64_t total) {
        if (!alive->load()) return;
        slint::invoke_from_event_loop([this, alive, download_id, received, total] {
            if (alive->load()) {
                const int index = findDownloadIndex(download_id);
                if (index >= 0) {
                    download_history_[static_cast<std::size_t>(index)].progress =
                        formatFileSize(received) + " / " + formatFileSize(total);
                    refreshHistory();
                }
            }
        });
    };
    callbacks.speed_changed = [this, alive, download_id](std::int64_t speed) {
        if (!alive->load()) return;
        slint::invoke_from_event_loop([this, alive, download_id, speed] {
            if (alive->load()) {
                const int index = findDownloadIndex(download_id);
                if (index >= 0) {
                    download_history_[static_cast<std::size_t>(index)].speed =
                        formatFileSize(speed) + "/s";
                    refreshHistory();
                }
            }
        });
    };
    callbacks.status_changed = [this, alive, download_id](const std::string &status) {
        if (!alive->load()) return;
        slint::invoke_from_event_loop([this, alive, download_id, status] {
            if (alive->load()) {
                const int index = findDownloadIndex(download_id);
                if (index >= 0) {
                    download_history_[static_cast<std::size_t>(index)].progress = status;
                    refreshHistory();
                }
            }
        });
    };
    callbacks.title_found = [this, alive, download_id](const std::string &title) {
        if (!alive->load()) return;
        slint::invoke_from_event_loop([this, alive, download_id, title] {
            if (alive->load()) {
                const int index = findDownloadIndex(download_id);
                if (index >= 0) {
                    download_history_[static_cast<std::size_t>(index)].title = title;
                    refreshHistory();
                }
            }
        });
    };
    callbacks.finished = [this, alive, download_id](int status,
                                                   const std::filesystem::path &file_path) {
        if (!alive->load()) return;
        slint::invoke_from_event_loop([this, alive, download_id, status, file_path] {
            if (!alive->load()) return;
            auto found = std::find_if(active_downloads_.begin(), active_downloads_.end(),
                                      [download_id](const auto &entry) {
                                          return entry.second == download_id;
                                      });
            if (found != active_downloads_.end()) {
                onDownloadFinished(found->first, status, file_path);
            }
        });
    };

    auto worker = std::make_unique<DownloadThread>(
        download_history_.front().url, getOutputDir(), ytdlp_path, getAudioQuality(),
        std::move(callbacks));
    DownloadThread *worker_pointer = worker.get();
    active_downloads_.emplace(worker_pointer, download_id);
    download_workers_.push_back(std::move(worker));
    updateDownloadButton();
    refreshStatus();
    worker_pointer->start();
}

void MainWindow::processDownloadQueue() {
    while (!download_queue_.empty() && active_downloads_.size() < MaxConcurrentDownloads) {
        const std::uint64_t id = download_queue_.front();
        download_queue_.erase(download_queue_.begin());
        startDownload(id);
    }
    updateDownloadButton();
    refreshStatus();
}

void MainWindow::onDownloadFinished(DownloadThread *download, int status,
                                    const std::filesystem::path &file_path) {
    const auto active = active_downloads_.find(download);
    if (active == active_downloads_.end()) return;
    const std::uint64_t download_id = active->second;
    active_downloads_.erase(active);
    const int index = findDownloadIndex(download_id);
    if (index >= 0) {
        DownloadInfo &info = download_history_[static_cast<std::size_t>(index)];
        const int elapsed = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - info.start_time).count());
        if (status == 0 && !file_path.empty()) {
            std::error_code error;
            info.status = 2;
            info.file_path = file_path;
            info.file_size = std::filesystem::file_size(file_path, error);
            info.progress = formatFileSize(static_cast<std::int64_t>(info.file_size));
        } else {
            info.status = 3;
            info.progress = status == -2 ? "Cancelled" : "Failed";
        }
        info.speed = formatDuration(elapsed);
    }

    download->join();
    download_workers_.erase(
        std::remove_if(download_workers_.begin(), download_workers_.end(),
                       [download](const auto &worker) { return worker.get() == download; }),
        download_workers_.end());
    refreshHistory();
    updateDownloadButton();
    processDownloadQueue();
    if (active_downloads_.empty() && download_queue_.empty()) refreshStatus(status);
}

int MainWindow::findDownloadIndex(std::uint64_t download_id) const {
    for (std::size_t index = 0; index < download_history_.size(); ++index) {
        if (download_history_[index].id == download_id) return static_cast<int>(index);
    }
    return -1;
}

void MainWindow::refreshHistory() {
    std::vector<DownloadItem> entries;
    entries.reserve(download_history_.size());
    for (const DownloadInfo &info : download_history_) {
        std::string status;
        switch (info.status) {
        case 0: status = "Queued"; break;
        case 1: status = info.progress == "Encoding" ? "Encoding" : "Downloading..."; break;
        case 2: status = "Complete"; break;
        default: status = info.progress == "Cancelled" ? "Cancelled" : "Failed"; break;
        }
        entries.push_back(DownloadItem{
            toSlintString(info.title), toSlintString(status), toSlintString(info.progress),
            toSlintString(info.speed), toSlintString(pathToUtf8(info.file_path)), info.status
        });
    }
    history_model_->set_vector(std::move(entries));
}

void MainWindow::refreshStatus(int last_status) {
    if (!active_downloads_.empty()) {
        window_->set_status_text(toSlintString(
            std::to_string(active_downloads_.size()) + " download(s) active"));
    } else if (!download_queue_.empty()) {
        window_->set_status_text("Queued");
    } else if (last_status != -1) {
        window_->set_status_text(last_status == 0 ? "Complete" : "Ready");
    } else {
        window_->set_status_text("Ready");
    }
}

void MainWindow::updateDownloadButton() {
    if (active_downloads_.empty() && download_queue_.empty()) {
        window_->set_download_button_text("Download");
    } else if (download_queue_.empty()) {
        window_->set_download_button_text("Queue");
    } else {
        window_->set_download_button_text(
            toSlintString("Queue (" + std::to_string(download_queue_.size()) + ")"));
    }
}

void MainWindow::openFile(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= download_history_.size()) return;
    const auto &path = download_history_[static_cast<std::size_t>(index)].file_path;
    std::error_code error;
    if (!path.empty() && std::filesystem::is_regular_file(path, error)) openWithSystem(path);
}

void MainWindow::openFolder(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= download_history_.size()) return;
    const auto &path = download_history_[static_cast<std::size_t>(index)].file_path;
    if (!path.empty()) openWithSystem(path.parent_path());
}

void MainWindow::removeItem(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= download_history_.size()) return;
    auto &info = download_history_[static_cast<std::size_t>(index)];
    if (info.status == 1) return;
    if (info.status == 0) {
        download_queue_.erase(std::remove(download_queue_.begin(), download_queue_.end(), info.id),
                              download_queue_.end());
    }
    download_history_.erase(download_history_.begin() + index);
    refreshHistory();
    updateDownloadButton();
    refreshStatus();
}

std::string MainWindow::formatFileSize(std::int64_t bytes) {
    if (bytes <= 0) return "0 B";
    constexpr std::array<const char *, 5> units = {"B", "KB", "MB", "GB", "TB"};
    std::size_t unit = 0;
    double size = static_cast<double>(bytes);
    while (size >= SizeThreshold && unit < 4) {
        size /= SizeThreshold;
        ++unit;
    }
    std::ostringstream output;
    output << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << size << ' ' << units[unit];
    return output.str();
}

std::string MainWindow::formatDuration(int seconds) {
    if (seconds < 60) return std::to_string(seconds) + "s";
    if (seconds < 3600) {
        return std::to_string(seconds / 60) + "m " + std::to_string(seconds % 60) + "s";
    }
    return std::to_string(seconds / 3600) + "h " +
        std::to_string((seconds % 3600) / 60) + "m";
}
