#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

class ReleaseChecker final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY statusChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY statusChanged)

public:
    explicit ReleaseChecker(QString currentVersion, QObject *parent = nullptr);
    QString status() const { return m_status; }
    QString latestVersion() const { return m_latestVersion; }
    bool updateAvailable() const { return m_updateAvailable; }

    Q_INVOKABLE void check();
    Q_INVOKABLE void openReleases() const;
    Q_INVOKABLE void openRepository() const;

signals:
    void statusChanged();

private:
    void setStatus(QString message);
    QString m_currentVersion;
    QString m_status;
    QString m_latestVersion;
    bool m_updateAvailable{false};
    bool m_requestActive{false};
    QNetworkAccessManager *m_network{nullptr};
};
