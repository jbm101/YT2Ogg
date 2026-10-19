#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QFileDialog>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVector>

#include "downloadthread.h"
#include "ytdlmanager.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    MainWindow(const MainWindow &) = delete;
    MainWindow &operator=(const MainWindow &) = delete;
    MainWindow(MainWindow &&) noexcept = delete;
    MainWindow &operator=(MainWindow &&) noexcept = delete;

private slots:
    void onDownloadClicked();
    void onBrowseClicked();
    void onItemDoubleClicked(QTableWidgetItem *item);
    void onContextMenu(const QPoint &pos);
    void onRemoveItem(QTableWidgetItem *item);
    void onClearAll();
    void onOpenDownloadFolder();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void processDownloadQueue();

    void closeEvent(QCloseEvent *event) override;

private:
    void setupUI();
    void setupTrayIcon();
    void loadSettings();
    void saveSettings();
    [[nodiscard]] QString getOutputDir() const;
    [[nodiscard]] int getAudioQuality() const;
    quint64 addDownloadItem(const QString &url, const QString &title);
    void startDownload(quint64 downloadId);
    void onDownloadFinished(DownloadThread *download, int status, const QString &filePath);
    void onDownloadProgress(DownloadThread *download, qint64 received, qint64 total);
    void onDownloadSpeedChanged(DownloadThread *download, qint64 speed);
    void onDownloadStatusChanged(DownloadThread *download, const QString &status);
    [[nodiscard]] int findDownloadIndex(quint64 downloadId) const;
    void updateDownloadButton();
    void updateStatsLabel(int lastStatus = -1);
    static QString formatFileSize(qint64 bytes);
    static QString formatDuration(int seconds);

protected:
    void resizeEvent(QResizeEvent *event) override;

    QLineEdit *urlInput_ = nullptr;
    QPushButton *downloadBtn_ = nullptr;
    QPushButton *browseBtn_ = nullptr;
    QLabel *outputDirLabel_ = nullptr;
    QLabel *statsLabel_ = nullptr;
    QSlider *qualitySlider_ = nullptr;
    QLabel *qualitySliderLabel_ = nullptr;

    QTableWidget *historyTable_ = nullptr;

    // State
    YtDlManager *ytDlManager_ = nullptr;
    QSystemTrayIcon *trayIcon_ = nullptr;
    QWidget *qualityWidget_ = nullptr;
    QSettings *settings_ = nullptr;
    QHash<DownloadThread *, quint64> activeDownloads_;
    QVector<quint64> downloadQueue_;
    quint64 nextDownloadId_ = 1;

    void showStartupDialog();
    struct DownloadInfo {
        quint64 id = 0;
        QString url;
        QString title;
        QString filePath;
        qint64 fileSize = 0;
        int status = 0; // 0=queued, 1=downloading, 2=completed, 3=error
        QDateTime startTime;
    };
    QVector<DownloadInfo> downloadHistory_;
};

#endif // MAINWINDOW_H
