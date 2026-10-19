#ifndef DOWNLOADTHREAD_H
#define DOWNLOADTHREAD_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

class DownloadThread {
public:
    struct Callbacks {
        std::function<void(std::int64_t, std::int64_t)> progress;
        std::function<void(std::int64_t)> speed_changed;
        std::function<void(const std::string &)> status_changed;
        std::function<void(const std::string &)> title_found;
        std::function<void(int, const std::filesystem::path &)> finished;
    };

    DownloadThread(std::string url, std::filesystem::path output_directory,
                   std::filesystem::path ytdlp_path, int quality, Callbacks callbacks);
    ~DownloadThread();

    DownloadThread(const DownloadThread &) = delete;
    DownloadThread &operator=(const DownloadThread &) = delete;
    DownloadThread(DownloadThread &&) = delete;
    DownloadThread &operator=(DownloadThread &&) = delete;

    void start();
    void cancel();
    void join();

private:
    void run();
    void processLine(const std::string &line);
    bool parseProgressLine(const std::string &line);
    void finish(int status, const std::filesystem::path &path = {});

    std::string url_;
    std::filesystem::path output_directory_;
    std::filesystem::path ytdlp_path_;
    int quality_ = 0;
    Callbacks callbacks_;
    std::atomic_bool cancelled_{false};
    std::thread thread_;
    std::filesystem::path output_file_path_;
};

#endif
