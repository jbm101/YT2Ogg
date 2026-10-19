#include "downloadthread.h"
#include <array>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>

namespace {
    constexpr auto TitleOutputPrefix = "__YT2OGG_TITLE__";
    constexpr int StartWaitTimeoutMs = 5000;
    constexpr int ReadWaitIntervalMs = 100;
    constexpr int Gigabytes = 1073741824;
    constexpr int Megabytes = 1048576;
    constexpr int Kilobytes = 1024;
}

DownloadThread::DownloadThread(QObject *parent)
    : QThread(parent)
    , cancelled_(false)
{
}

DownloadThread::~DownloadThread() {
    if (isRunning()) {
        cancel();
        wait();
    }
}

void DownloadThread::startDownload(const QString &url, const QString &outputDir,
                                   const QString &ytDlpPath, int quality) {
    url_ = url;
    outputDir_ = outputDir;
    ytDlpPath_ = ytDlpPath;
    quality_ = qBound(0, quality, 3);
    cancelled_.store(false);
    outputFilePath_.clear();

    start();
}

void DownloadThread::cancel() {
    cancelled_.store(true);
}

void DownloadThread::run() {
    QProcess process;

    QStringList args;
    args << "--no-playlist"
         << "--newline"
         << "--progress"
         << "--no-simulate"
         << "--print" << "after_move:filepath"
         << "--print" << QString("before_dl:%1%(title)s").arg(TitleOutputPrefix)
         << "--no-quiet"
         << "-f" << "bestaudio[acodec=opus]"
         << "-x"
         << "--audio-format" << "opus";

    constexpr std::array<const char *, 3> qualityBitrates = {"96K", "80K", "64K"};
    if (quality_ > 0) {
        const QString bitrate = QString::fromLatin1(qualityBitrates[quality_ - 1]);
        args << "--audio-quality" << bitrate
             << "--postprocessor-args"
             << QString("ExtractAudio+ffmpeg_o:-c:a libopus -b:a %1").arg(bitrate);
    }

    args << "-o" << QString("%1/%(title)s.%(ext)s").arg(outputDir_)
         << "--" << url_;

    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(ytDlpPath_, args);

    if (!process.waitForStarted(StartWaitTimeoutMs)) {
        emit finished(-1, QString());
        return;
    }

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(ReadWaitIntervalMs);
        onReadyReadStandardOutput(&process);
    }
    onReadyReadStandardOutput(&process);
    onFinished(&process, process.exitCode(), process.exitStatus());
}

void DownloadThread::onReadyReadStandardOutput(QProcess *process) {
    while (process->canReadLine()) {
        QString line = QString::fromUtf8(process->readLine()).trimmed();
        if (line.isEmpty()) {
            continue;
        }

        if (cancelled_.load()) {
            process->kill();
            return;
        }

        if (parseProgressLine(line)) {
            continue;
        }

        if (line.startsWith(TitleOutputPrefix)) {
            emit titleFound(line.mid(QString::fromLatin1(TitleOutputPrefix).size()));
            continue;
        }

        if (line.contains("[ExtractAudio]", Qt::CaseInsensitive)) {
            emit statusChanged(tr("Encoding"));
        }

        const QFileInfo printedFile(line);
        if (printedFile.suffix().compare("opus", Qt::CaseInsensitive) == 0 &&
            printedFile.isFile()) {
            outputFilePath_ = printedFile.absoluteFilePath();
        }
    }
}

void DownloadThread::onFinished(QProcess *process, int exitCode, QProcess::ExitStatus exitStatus) {
    if (cancelled_.load()) {
        emit finished(-2, QString());
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        emit finished(-3, QString());
        return;
    }
    const QFileInfo opusFile(outputFilePath_);
    if (opusFile.exists()) {
        QString oggPath = opusFile.dir().filePath(opusFile.completeBaseName() + ".ogg");
        if (QFileInfo::exists(oggPath)) {
            int suffix = 1;
            do {
                oggPath = opusFile.dir().filePath(
                    QString("%1 (%2).ogg").arg(opusFile.completeBaseName()).arg(suffix++));
            } while (QFileInfo::exists(oggPath));
        }
        if (!QFile::rename(opusFile.absoluteFilePath(), oggPath)) {
            emit finished(-4, QString());
            return;
        }
        emit finished(0, oggPath);
    } else {
        emit finished(-4, QString());
    }
}

auto DownloadThread::parseProgressLine(const QString &line) -> bool {
    QRegularExpression regex(
        R"(\[download\]\s+(\d+\.?\d*)%\s+of\s+([\d.]+)([KMGT]?i?B?)\s+at\s+([\d.]+)([KMGT]?i?B?/s)\s+ETA\s+(?:\d+:\d+|0:00))");
    QRegularExpressionMatch match = regex.match(line);

    if (!match.hasMatch()) {
        return false;
    }

    double percent = match.captured(1).toDouble();

    double totalVal = match.captured(2).toDouble();
    QString totalUnit = match.captured(3).toUpper();
    qint64 totalBytes = 0;
    if (totalUnit.contains("G")) {
        totalBytes = static_cast<qint64>(totalVal * Gigabytes);
    } else if (totalUnit.contains("M")) {
        totalBytes = static_cast<qint64>(totalVal * Megabytes);
    } else if (totalUnit.contains("K")) {
        totalBytes = static_cast<qint64>(totalVal * Kilobytes);
    } else {
        totalBytes = static_cast<qint64>(totalVal);
    }

    double speedVal = match.captured(4).toDouble();
    QString speedUnit = match.captured(5).toUpper();
    qint64 speedBytes = 0;
    if (speedUnit.contains("G")) {
        speedBytes = static_cast<qint64>(speedVal * Gigabytes);
    } else if (speedUnit.contains("M")) {
        speedBytes = static_cast<qint64>(speedVal * Megabytes);
    } else if (speedUnit.contains("K")) {
        speedBytes = static_cast<qint64>(speedVal * Kilobytes);
    } else {
        speedBytes = static_cast<qint64>(speedVal);
    }

    auto receivedBytes = static_cast<qint64>(static_cast<double>(totalBytes) * percent / 100.0);

    emit progress(receivedBytes, totalBytes);
    emit speedChanged(speedBytes);

    return true;
}
