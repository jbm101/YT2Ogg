#include "mainwindow.h"
#include <array>
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFile>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSizePolicy>
#include <QSlider>
#include <QStandardPaths>
#include <QStyle>
#include <QSysInfo>
#include <QTimer>

namespace {
    constexpr int WindowMinWidth = 700;
    constexpr int WindowMinHeight = 550;
    constexpr int WindowInitWidth = 750;
    constexpr int WindowInitHeight = 600;
    constexpr int LayoutSpacing = 12;
    constexpr int LayoutMargins = 16;
    constexpr int GroupMargins = 12;
    constexpr int ButtonMinWidth = 120;
    constexpr int ButtonMaxWidth = 160;
    constexpr int UrlInputHeight = 40;
    constexpr int QualityLabelWidth = 90;
    constexpr int HistoryRowHeight = 28;
    constexpr int DialogWidth = 420;
    constexpr int DialogHeight = 220;
    constexpr int DialogMarginLeft = 24;
    constexpr int DialogMarginTop = 20;
    constexpr int DialogMarginRight = 24;
    constexpr int DialogMarginBottom = 20;
    constexpr int DialogSpacing = 16;
    constexpr int ProgressBarMax = 100;
    constexpr int ButtonWidth = 80;
    constexpr int ResizeMarginOffset = 32;
    constexpr int SpeedThreshold = 1024;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)

{
    // Initialize settings
    settings_ = new QSettings("YT2Ogg", "YT2Ogg", this);

    setupUI();
    loadSettings();

    setWindowTitle(tr("YT2Ogg - YouTube to Opus"));
    setMinimumSize(WindowMinWidth, WindowMinHeight);
    resize(WindowInitWidth, WindowInitHeight);
}

MainWindow::~MainWindow() {
    const auto downloads = activeDownloads_.keys();
    for (DownloadThread *download : downloads) {
        download->cancel();
    }
    for (DownloadThread *download : downloads) {
        download->wait();
    }
}

void MainWindow::setupUI() {
    auto *central = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(LayoutSpacing);
    mainLayout->setContentsMargins(LayoutMargins, LayoutSpacing, LayoutMargins, LayoutSpacing);

    auto *urlGroup = new QWidget(central);
    auto *urlLayout = new QVBoxLayout(urlGroup);
    urlLayout->setContentsMargins(GroupMargins, GroupMargins, GroupMargins, GroupMargins);

    auto *urlRow = new QHBoxLayout();
    urlInput_ = new QLineEdit(urlGroup);
    urlInput_->setPlaceholderText(
        tr("Paste YouTube URL here... (e.g., https://youtube.com/watch?v=...)"));
    urlInput_->setMinimumHeight(UrlInputHeight);

    // URL validation with QRegularExpressionValidator
    QRegularExpression urlRe(
        R"(https?://(?:www\.)?(?:youtube\.com/(?:watch\?v=|shorts/)|youtu\.be/)\S+)");
    urlInput_->setValidator(new QRegularExpressionValidator(urlRe, urlInput_));

    auto *pasteAction = new QAction(this);
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, [this]() {
        urlInput_->setText(QApplication::clipboard()->text());
    });
    addAction(pasteAction);

    urlRow->addWidget(urlInput_);

    downloadBtn_ = new QPushButton(tr("Download"), urlGroup);
    downloadBtn_->setMinimumWidth(ButtonMinWidth);
    downloadBtn_->setMaximumWidth(ButtonMaxWidth);
    downloadBtn_->setCursor(Qt::PointingHandCursor);
    connect(downloadBtn_, &QPushButton::clicked, this, &MainWindow::onDownloadClicked);
    urlRow->addWidget(downloadBtn_);

    browseBtn_ = new QPushButton(qApp->style()->standardIcon(QStyle::SP_DirIcon),
                                  tr("Set Output Folder"), urlGroup);
    browseBtn_->setCursor(Qt::PointingHandCursor);
    connect(browseBtn_, &QPushButton::clicked, this, &MainWindow::onBrowseClicked);
    urlRow->addWidget(browseBtn_);

    urlLayout->addLayout(urlRow);

    outputDirLabel_ = new QLabel(urlGroup);
    outputDirLabel_->setText(tr("Output: %1").arg(getOutputDir()));
    urlLayout->addWidget(outputDirLabel_);

    mainLayout->addWidget(urlGroup);

    mainLayout->addSpacing(4);

    qualityWidget_ = new QWidget(central);
    qualityWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *qualityRow = new QHBoxLayout(qualityWidget_);
    qualityRow->setContentsMargins(0, 0, 0, 0);

    auto *qualityLabel = new QLabel(tr("Audio Quality:"), central);
    qualitySlider_ = new QSlider(Qt::Horizontal, central);
    qualitySlider_->setRange(0, 3);
    qualitySlider_->setValue(0);
    qualitySlider_->setTickPosition(QSlider::TicksBelow);
    qualitySlider_->setTickInterval(1);
    qualitySlider_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    qualitySliderLabel_ = new QLabel(tr("Source"), central);
    qualitySliderLabel_->setFixedWidth(QualityLabelWidth);
    connect(qualitySlider_, &QSlider::valueChanged,
            this, [this](int v) {
                constexpr std::array<const char *, 4> qualityLabels = {"Source", "96 kbps", "80 kbps", "64 kbps"};
                qualitySliderLabel_->setText(tr("%1").arg(qualityLabels[v]));
                saveSettings();
            });

    qualityRow->addWidget(qualityLabel);
    qualityRow->addWidget(qualitySlider_);
    qualityRow->addWidget(qualitySliderLabel_);
    mainLayout->addWidget(qualityWidget_);
    mainLayout->addStretch();

    auto *historyGroup = new QWidget(central);
    auto *historyLayout = new QVBoxLayout(historyGroup);
    historyLayout->setContentsMargins(GroupMargins, GroupMargins, GroupMargins, GroupMargins);

    historyTable_ = new QTableWidget(historyGroup);
    historyTable_->setColumnCount(4);
    historyTable_->setHorizontalHeaderLabels(
        {tr("Title"), tr("Status"), tr("Size"), tr("Speed")});
    historyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    historyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    historyTable_->setAlternatingRowColors(true);
    historyTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    historyTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    historyTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    historyTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    historyTable_->verticalHeader()->setVisible(true);
    historyTable_->verticalHeader()->setDefaultSectionSize(HistoryRowHeight);
    historyTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(historyTable_, &QTableWidget::itemDoubleClicked,
            this, &MainWindow::onItemDoubleClicked);
    connect(historyTable_, &QTableWidget::customContextMenuRequested,
            this, &MainWindow::onContextMenu);

    historyLayout->addWidget(historyTable_);

    auto *historyButtons = new QHBoxLayout();
    historyButtons->addStretch();

    auto *clearAllBtn = new QPushButton(tr("Clear All"), historyGroup);
    connect(clearAllBtn, &QPushButton::clicked, this, &MainWindow::onClearAll);
    historyButtons->addWidget(clearAllBtn);

    historyLayout->addLayout(historyButtons);
    mainLayout->addWidget(historyGroup, 1);

    statsLabel_ = new QLabel(tr("Ready"), central);
    statsLabel_->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(statsLabel_);

    setCentralWidget(central);
}

void MainWindow::loadSettings() {
    // Restore download directory (with default fallback)
    QString downloadDir = settings_->value("downloadDir", "").toString();
    if (downloadDir.isEmpty()) {
        downloadDir = QStandardPaths::writableLocation(
            QStandardPaths::DownloadLocation);
        if (downloadDir.isEmpty()) {
            downloadDir = QDir::homePath() + "/Downloads";
        }
        settings_->setValue("downloadDir", downloadDir);
    }

    // Restore quality setting
    int quality = settings_->value("quality", 0).toInt();
    quality = qBound(0, quality, 3);
    qualitySlider_->setValue(quality);
}

void MainWindow::saveSettings() {
    QString downloadDir = getOutputDir();
    if (downloadDir.isEmpty()) {
        downloadDir = QStandardPaths::writableLocation(
            QStandardPaths::DownloadLocation);
    }
    settings_->setValue("downloadDir", downloadDir);
    settings_->setValue("quality", qualitySlider_->value());
}

QString MainWindow::getOutputDir() const {
    return settings_->value("downloadDir", "").toString();
}

int MainWindow::getAudioQuality() const {
    return qualitySlider_->value();
}

void MainWindow::startDownload(quint64 downloadId) {
    const int index = findDownloadIndex(downloadId);
    if (index < 0) {
        return;
    }

    QList<QTableWidgetItem *> items;
    for (int column = 0; column < historyTable_->columnCount(); ++column) {
        items.append(historyTable_->takeItem(index, column));
    }
    historyTable_->removeRow(index);
    historyTable_->insertRow(0);
    for (int column = 0; column < items.size(); ++column) {
        historyTable_->setItem(0, column, items[column]);
    }
    downloadHistory_.prepend(downloadHistory_.takeAt(index));
    DownloadInfo &info = downloadHistory_.first();
    info.status = 1;
    info.startTime = QDateTime::currentDateTime();
    historyTable_->setItem(0, 1, new QTableWidgetItem(tr("Downloading...")));

    auto *download = new DownloadThread(this);
    activeDownloads_.insert(download, downloadId);
    connect(download, &DownloadThread::progress, this,
            [this, download](qint64 received, qint64 total) {
                onDownloadProgress(download, received, total);
            });
    connect(download, &DownloadThread::speedChanged, this,
            [this, download](qint64 speed) {
                onDownloadSpeedChanged(download, speed);
            });
    connect(download, &DownloadThread::statusChanged, this,
            [this, download](const QString &status) {
                onDownloadStatusChanged(download, status);
            });
    connect(download, &DownloadThread::titleFound, this,
            [this, download](const QString &title) {
                const int row = findDownloadIndex(activeDownloads_.value(download));
                if (row < 0) {
                    return;
                }
                downloadHistory_[row].title = title;
                historyTable_->setItem(row, 0, new QTableWidgetItem(title));
            });
    connect(download, &DownloadThread::finished, this,
            [this, download](int status, const QString &filePath) {
                onDownloadFinished(download, status, filePath);
            });

    updateDownloadButton();
    updateStatsLabel();
    QString ytDlpPath = QCoreApplication::applicationDirPath() + "/" +
                        (QSysInfo::productType() == "windows" ? "yt-dlp.exe" : "yt-dlp");
    download->startDownload(info.url, getOutputDir(), ytDlpPath,
                            getAudioQuality());
}

void MainWindow::onDownloadClicked() {
    QString url = urlInput_->text().trimmed();
    if (url.isEmpty()) {
        QMessageBox::warning(this, tr("No URL"),
                             tr("Please enter a YouTube URL."));
        return;
    }

    if (!url.startsWith("http://") && !url.startsWith("https://")) {
        QMessageBox::warning(this, tr("Invalid URL"),
                             tr("Please enter a valid URL starting with http:// or https://"));
        return;
    }

    QString outputDir = getOutputDir();
    if (outputDir.isEmpty() || !QDir(outputDir).exists()) {
        onBrowseClicked();
        if (getOutputDir().isEmpty()) return;
        outputDir = getOutputDir();
    }

    urlInput_->clear();
    const quint64 downloadId = addDownloadItem(url, QString());
    downloadQueue_.append(downloadId);
    processDownloadQueue();
    updateDownloadButton();
}

void MainWindow::onBrowseClicked() {
    QString dir = QFileDialog::getExistingDirectory(this,
        tr("Set Output Folder"),
        getOutputDir(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()) {
        settings_->setValue("downloadDir", dir);
        browseBtn_->setText(dir);
        outputDirLabel_->setText(tr("Output: %1").arg(dir));
    }
}

void MainWindow::onDownloadProgress(DownloadThread *download, qint64 received, qint64 total) {
    const int row = findDownloadIndex(activeDownloads_.value(download));
    if (row >= 0) {
        historyTable_->setItem(row, 2, new QTableWidgetItem(
            tr("%1 / %2")
                .arg(formatFileSize(received))
                .arg(formatFileSize(total))));
    }
}

void MainWindow::onDownloadSpeedChanged(DownloadThread *download, qint64 speed) {
    const int row = findDownloadIndex(activeDownloads_.value(download));
    if (row >= 0) {
        historyTable_->setItem(row, 3, new QTableWidgetItem(
            tr("%1/s").arg(formatFileSize(speed))));
    }
}

void MainWindow::onDownloadStatusChanged(DownloadThread *download, const QString &status) {
    const int row = findDownloadIndex(activeDownloads_.value(download));
    if (row >= 0) {
        historyTable_->setItem(row, 1, new QTableWidgetItem(status));
    }
}

void MainWindow::onDownloadFinished(DownloadThread *download, int status,
                                    const QString &filePath) {
    const quint64 downloadId = activeDownloads_.take(download);
    const int row = findDownloadIndex(downloadId);
    if (row >= 0) {
        DownloadInfo &info = downloadHistory_[row];
        const int elapsed = static_cast<int>(info.startTime.secsTo(QDateTime::currentDateTime()));

        if (status == 0 && !filePath.isEmpty()) {
            historyTable_->setItem(row, 1, new QTableWidgetItem(tr("Complete")));
            const QFileInfo fileInfo(filePath);
            historyTable_->setItem(row, 2, new QTableWidgetItem(formatFileSize(fileInfo.size())));
            info.filePath = filePath;
            info.fileSize = fileInfo.size();
            info.status = 2;
        } else if (status == -2) {
            historyTable_->setItem(row, 1, new QTableWidgetItem(tr("Cancelled")));
            info.status = 3;
        } else {
            historyTable_->setItem(row, 1, new QTableWidgetItem(tr("Failed")));
            info.status = 3;
        }
        historyTable_->setItem(row, 3, new QTableWidgetItem(formatDuration(elapsed)));
    }

    download->deleteLater();
    updateDownloadButton();
    processDownloadQueue();
    if (activeDownloads_.isEmpty() && downloadQueue_.isEmpty()) {
        updateStatsLabel(status);
    }
}

void MainWindow::processDownloadQueue() {
    constexpr int MaxConcurrentDownloads = 3;
    while (!downloadQueue_.isEmpty() &&
           activeDownloads_.size() < MaxConcurrentDownloads) {
        const quint64 downloadId = downloadQueue_.takeFirst();
        startDownload(downloadId);
    }
    updateDownloadButton();
    updateStatsLabel();
}

void MainWindow::onItemDoubleClicked(QTableWidgetItem *item) {
    int row = item->row();
    if (row >= 0 && row < static_cast<int>(downloadHistory_.size())) {
        const DownloadInfo &info = downloadHistory_[row];
        if (!info.filePath.isEmpty() && QFile::exists(info.filePath)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(info.filePath));
        }
    }
}

void MainWindow::onContextMenu(const QPoint &pos) {
    QTableWidgetItem *item = historyTable_->itemAt(pos);
    if (!item) return;

    int row = item->row();
    QMenu menu(this);

    auto *openAction = menu.addAction(tr("Open File"));
    auto *openFolderAction = menu.addAction(tr("Open Containing Folder"));
    auto *removeAction = menu.addAction(tr("Remove"));
    if (row < downloadHistory_.size() && downloadHistory_[row].status == 1) {
        removeAction->setEnabled(false);
    }

    QAction *selected = menu.exec(historyTable_->viewport()->mapToGlobal(pos));
    if (selected == openAction) {
        onItemDoubleClicked(item);
    } else if (selected == openFolderAction) {
        if (row < static_cast<int>(downloadHistory_.size()) &&
            !downloadHistory_[row].filePath.isEmpty()) {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(
                    QFileInfo(downloadHistory_[row].filePath).path()));
        }
    } else if (selected == removeAction) {
        onRemoveItem(item);
    }
}

void MainWindow::onRemoveItem(QTableWidgetItem *item) {
    int row = item->row();
    if (row >= 0 && row < static_cast<int>(downloadHistory_.size())) {
        if (downloadHistory_[row].status == 1) {
            return;
        }
        if (downloadHistory_[row].status == 0) {
            downloadQueue_.removeAll(downloadHistory_[row].id);
        }
        downloadHistory_.removeAt(row);
        historyTable_->removeRow(row);
        updateDownloadButton();
    }
}

void MainWindow::onClearAll() {
    for (int row = static_cast<int>(downloadHistory_.size()) - 1; row >= 0; --row) {
        if (downloadHistory_[row].status != 1) {
            historyTable_->removeRow(row);
            downloadHistory_.removeAt(row);
        }
    }
    downloadQueue_.clear();
    updateDownloadButton();
    updateStatsLabel();
}

void MainWindow::onOpenDownloadFolder() {
    QString dir = getOutputDir();
    if (dir.isEmpty()) dir = QDir::homePath() + "/Downloads";
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

quint64 MainWindow::addDownloadItem(const QString &url, const QString &title) {
    QString displayTitle = title;
    if (displayTitle.isEmpty()) {
        QUrl qUrl(url);
        displayTitle = qUrl.host() + qUrl.path();
    }

    const int row = historyTable_->rowCount();
    historyTable_->insertRow(row);
    historyTable_->setItem(row, 0, new QTableWidgetItem(displayTitle));
    historyTable_->setItem(row, 1, new QTableWidgetItem(tr("Queued")));
    historyTable_->setItem(row, 2, new QTableWidgetItem(tr("-")));
    historyTable_->setItem(row, 3, new QTableWidgetItem(tr("-")));

    DownloadInfo info;
    info.id = nextDownloadId_++;
    info.url = url;
    info.title = title;
    info.status = 0;
    downloadHistory_.append(info);
    return info.id;
}

int MainWindow::findDownloadIndex(quint64 downloadId) const {
    for (int i = 0; i < static_cast<int>(downloadHistory_.size()); ++i) {
        if (downloadHistory_[i].id == downloadId) {
            return i;
        }
    }
    return -1;
}

void MainWindow::updateDownloadButton() {
    if (activeDownloads_.isEmpty() && downloadQueue_.isEmpty()) {
        downloadBtn_->setText(tr("Download"));
    } else {
        downloadBtn_->setText(downloadQueue_.isEmpty()
                                  ? tr("Queue")
                                  : tr("Queue (%1)").arg(downloadQueue_.size()));
    }
}

void MainWindow::updateStatsLabel(int lastStatus) {
    if (!activeDownloads_.isEmpty()) {
        statsLabel_->setText(tr("%1 download(s) active").arg(activeDownloads_.size()));
    } else if (!downloadQueue_.isEmpty()) {
        statsLabel_->setText(tr("Queued"));
    } else if (lastStatus != -1) {
        statsLabel_->setText(lastStatus == 0 ? tr("Complete") : tr("Ready"));
    } else {
        statsLabel_->setText(tr("Ready"));
    }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (qualityWidget_ && centralWidget()) {
        int width = centralWidget()->width() - ResizeMarginOffset;
        qualityWidget_->setGeometry(LayoutMargins, qualityWidget_->geometry().y(), width, qualityWidget_->height());
    }
}

QString MainWindow::formatFileSize(qint64 bytes) {
    if (bytes <= 0) return "0 B";

    constexpr std::array<const char *, 5> units = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(bytes);

    while (size >= SpeedThreshold && unit < 4) {
        size /= SpeedThreshold;
        unit++;
    }

    return QString("%1 %2").arg(QString::number(size, 'f', unit == 0 ? 0 : 1)).arg(units[unit]);
}

QString MainWindow::formatDuration(int seconds) {
    if (seconds < 60) return QString("%1s").arg(seconds);
    if (seconds < 3600) {
        return QString("%1m %2s").arg(seconds / 60).arg(seconds % 60);
    }
    return QString("%1h %2m").arg(seconds / 3600).arg((seconds % 3600) / 60);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    const auto downloads = activeDownloads_.keys();
    for (DownloadThread *download : downloads) {
        download->cancel();
    }
    event->accept();
    qApp->quit();
}


