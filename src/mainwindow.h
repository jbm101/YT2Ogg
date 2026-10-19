#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <chrono>
#include <cstdint>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "downloadthread.h"
#include "generated_ui.h"

class MainWindow {
public:
    explicit MainWindow(std::filesystem::path executable_path);
    ~MainWindow();

    MainWindow(const MainWindow &) = delete;
    MainWindow &operator=(const MainWindow &) = delete;
    MainWindow(MainWindow &&) = delete;
    MainWindow &operator=(MainWindow &&) = delete;

    int run();

private:
    struct DownloadInfo {
        std::uint64_t id = 0;
        std::string url;
        std::string title;
        std::filesystem::path file_path;
        std::uintmax_t file_size = 0;
        int status = 0;
        std::chrono::steady_clock::time_point start_time;
        std::string progress = "-";
        std::string speed = "-";
    };

    void loadSettings();
    void saveSettings();
    std::filesystem::path getOutputDir() const;
    int getAudioQuality() const;
    std::uint64_t addDownloadItem(const std::string &url);
    void startDownload(std::uint64_t download_id);
    void processDownloadQueue();
    void onDownloadFinished(DownloadThread *download, int status,
                            const std::filesystem::path &file_path);
    int findDownloadIndex(std::uint64_t download_id) const;
    void refreshHistory();
    void refreshStatus(int last_status = -1);
    void updateDownloadButton();
    void openFile(int index);
    void openFolder(int index);
    void removeItem(int index);
    static std::string formatFileSize(std::int64_t bytes);
    static std::string formatDuration(int seconds);

    std::filesystem::path executable_path_;
    std::filesystem::path settings_path_;
    slint::ComponentHandle<MainWindowUI> window_;
    std::shared_ptr<slint::VectorModel<DownloadItem>> history_model_;
    std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
    std::unordered_map<DownloadThread *, std::uint64_t> active_downloads_;
    std::vector<std::unique_ptr<DownloadThread>> download_workers_;
    std::vector<std::uint64_t> download_queue_;
    std::vector<DownloadInfo> download_history_;
    std::uint64_t next_download_id_ = 1;
};

#endif
