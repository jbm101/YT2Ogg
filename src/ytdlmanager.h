#ifndef YTDLMANAGER_H
#define YTDLMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>

class YtDlManager : public QObject {
    Q_OBJECT

public:
    explicit YtDlManager(QObject *parent = nullptr);

    QString getYtDlpPath();

    static constexpr const char* YTDLP_FALLBACK_VERSION = "2026.08.19";

signals:
    void downloadError(const QString &error);
    void checkFinished(bool available, const QString &path);

public:
    bool checkYtDlpAvailable();

private:
    [[nodiscard]] QString getYtDlpUrl(const QString &platform) const;
    bool downloadFile(const QString &url, const QString &destPath);
    bool verifyExecutable(const QString &path);
    bool findYtDlpInKnownLocations(QString &outPath);

    QNetworkAccessManager *network_;
    QString platform_;
    QString ytDlpPath_;
    bool checked_ = false;
};

#endif // YTDLMANAGER_H
