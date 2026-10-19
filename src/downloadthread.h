#ifndef DOWNLOADTHREAD_H
#define DOWNLOADTHREAD_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QThread>
#include <atomic>

class DownloadThread : public QThread {
    Q_OBJECT

public:
    explicit DownloadThread(QObject *parent = nullptr);
    ~DownloadThread() override;

    DownloadThread(DownloadThread &&) = delete;
    DownloadThread &operator=(DownloadThread &&) = delete;

    DownloadThread(const DownloadThread &) = delete;
    DownloadThread &operator=(const DownloadThread &) = delete;

    void startDownload(const QString &url, const QString &outputDir,
                       const QString &ytDlpPath, int quality);
    void cancel();

    void run() override;

signals:
    void progress(qint64 received, qint64 total);
    void speedChanged(qint64 bytesPerSecond);
    void statusChanged(const QString &status);
    void titleFound(const QString &title);
    void finished(int status, const QString &filePath);

private:
    void onReadyReadStandardOutput(QProcess *process);
    void onFinished(QProcess *process, int exitCode, QProcess::ExitStatus exitStatus);
    bool parseProgressLine(const QString &line);

    QString url_;
    QString outputDir_;
    QString ytDlpPath_;
    int quality_ = 0;
    QString outputFilePath_;
    std::atomic_bool cancelled_;
};

#endif // DOWNLOADTHREAD_H
