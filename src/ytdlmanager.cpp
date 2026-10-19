#include "ytdlmanager.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {
    constexpr int NetworkTimeoutMs = 10000;
    constexpr int DownloadTimeoutMs = 60000;
    constexpr int VerifyTimeoutMs = 3000;
}

YtDlManager::YtDlManager(QObject *parent)
    : QObject(parent)
    , network_(new QNetworkAccessManager(this))
    , platform_(
#if defined(Q_OS_LINUX)
        "linux"
#elif defined(Q_OS_MAC)
        "macos"
#elif defined(Q_OS_WIN)
        "windows"
#else
        "linux"
#endif
    )
    , checked_(false)
{
}

auto YtDlManager::getYtDlpUrl(const QString &platform) const -> QString {
    // Fetch latest version from GitHub API
    QUrl apiUrl("https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest");

    QNetworkRequest request(apiUrl);
    request.setRawHeader("User-Agent", "YT2Ogg/1.0");
    request.setRawHeader("Accept", "application/vnd.github.v3+json");

    QEventLoop loop;
    QNetworkReply *reply = network_->get(request);
    QTimer::singleShot(NetworkTimeoutMs, &loop, &QEventLoop::quit);

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QString version;
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QRegularExpression verRe(R"YTDLP_VERSION_RE(\"tag_name\"\s*:\s*\"(\d+\.\d+\.\d+)\")YTDLP_VERSION_RE");
        QRegularExpressionMatch verMatch = verRe.match(QString(data));
        if (verMatch.hasMatch()) {
            version = verMatch.captured(1);
        }
    }
    reply->deleteLater();

    if (version.isEmpty()) {
        qWarning() << "Failed to fetch yt-dlp version from GitHub API, using fallback";
        version = YTDLP_FALLBACK_VERSION;
    }

    if (platform == "linux") {
        return QString("https://github.com/yt-dlp/yt-dlp/releases/download/%1/yt-dlp_linux")
                   .arg(version);
    }
    if (platform == "macos") {
        return QString("https://github.com/yt-dlp/yt-dlp/releases/download/%1/yt-dlp_macos")
                   .arg(version);
    }
    return QString("https://github.com/yt-dlp/yt-dlp/releases/download/%1/yt-dlp.exe")
               .arg(version);
}

bool YtDlManager::downloadFile(const QString &url, const QString &destPath) {
    QUrl qUrl(url);
    if (!qUrl.isValid()) {
        emit downloadError(tr("Invalid download URL"));
        return false;
    }

    QNetworkRequest request(qUrl);
    request.setRawHeader("User-Agent", "YT2Ogg/1.0");

    QEventLoop loop;
    QNetworkReply *reply = network_->get(request);
    QTimer::singleShot(DownloadTimeoutMs, &loop, &QEventLoop::quit);

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        emit downloadError(reply->errorString());
        reply->deleteLater();
        return false;
    }
    QDir destDir = QFileInfo(destPath).dir();
    if (!destDir.exists()) {
        destDir.mkpath(".");
    }

    QFile file(destPath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit downloadError(tr("Cannot write to %1").arg(destPath));
        reply->deleteLater();
        return false;
    }

    file.write(reply->readAll());
    file.close();
    reply->deleteLater();
    if (!destPath.endsWith(".exe")) {
        QFile::setPermissions(destPath,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
            QFileDevice::ReadGroup | QFileDevice::ExeGroup |
            QFileDevice::ReadOther | QFileDevice::ExeOther);
    }

    return verifyExecutable(destPath);
}

bool YtDlManager::findYtDlpInKnownLocations(QString &outPath) {
    QString appDir = QCoreApplication::applicationDirPath();
    QString filename = (platform_ == "windows") ? "yt-dlp.exe" : "yt-dlp";
    QString path = appDir + "/" + filename;

    if (QFile::exists(path) && verifyExecutable(path)) {
        outPath = path;
        return true;
    }

    QString resolved = QStandardPaths::findExecutable("yt-dlp");
    if (!resolved.isEmpty() && verifyExecutable(resolved)) {
        outPath = resolved;
        return true;
    }

    return false;
}

bool YtDlManager::verifyExecutable(const QString &path) {
    if (!QFile::exists(path)) {
        return false;
    }

    auto perms = QFile(path).permissions();
    if (!(perms & (QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther))) {
        return false;
    }
    QProcess proc;
    proc.start(path, {"--version"});
    proc.waitForFinished(VerifyTimeoutMs);

    return proc.state() == QProcess::NotRunning && proc.exitCode() == 0;
}

bool YtDlManager::checkYtDlpAvailable() {
    QString path;
    return findYtDlpInKnownLocations(path);
}

QString YtDlManager::getYtDlpPath() {
    if (checked_) {
        return ytDlpPath_;
    }

    checked_ = true;

    QString path;
    if (findYtDlpInKnownLocations(path)) {
        ytDlpPath_ = path;
        emit checkFinished(true, path);
        return ytDlpPath_;
    }

    QString url = getYtDlpUrl(platform_);
    bool success = downloadFile(url, path);

    if (!success) {
        emit downloadError(tr("Failed to download yt-dlp. Please install it manually."));
    }
    ytDlpPath_ = success ? path : QString();
    emit checkFinished(success, path);

    return ytDlpPath_;
}
