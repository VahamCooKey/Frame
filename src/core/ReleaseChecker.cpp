#include "ReleaseChecker.h"

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QVersionNumber>

namespace {
constexpr auto repositoryUrl = "https://github.com/VahamCooKey/Frame";
constexpr auto latestReleaseApi = "https://api.github.com/repos/VahamCooKey/Frame/releases/latest";

QVersionNumber normalizedVersion(QString value)
{
    if (value.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        value.remove(0, 1);
    return QVersionNumber::fromString(value);
}
}

ReleaseChecker::ReleaseChecker(QString currentVersion, QObject *parent)
    : QObject(parent), m_currentVersion(std::move(currentVersion)),
      m_network(new QNetworkAccessManager(this))
{
}

void ReleaseChecker::setStatus(QString message)
{
    m_status = std::move(message);
    emit statusChanged();
}

void ReleaseChecker::check()
{
    if (m_requestActive) return;
    m_requestActive = true;
    m_updateAvailable = false;
    m_latestVersion.clear();
    setStatus(QStringLiteral("Checking GitHub Releases…"));

    QNetworkRequest request{QUrl{QString::fromLatin1(latestReleaseApi)}};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Frame/%1").arg(m_currentVersion));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(10000);
    auto *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_requestActive = false;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code == 404) {
            setStatus(QStringLiteral("This repository is private. Open GitHub Releases in your browser."));
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            setStatus(QStringLiteral("Could not check GitHub Releases. Check your connection and try again."));
            reply->deleteLater();
            return;
        }

        const auto release = QJsonDocument::fromJson(reply->readAll()).object();
        const auto latest = normalizedVersion(release.value(QStringLiteral("tag_name")).toString());
        const auto current = normalizedVersion(m_currentVersion);
        if (latest.isNull() || current.isNull()) {
            setStatus(QStringLiteral("GitHub returned a release with an unreadable version."));
        } else {
            m_latestVersion = latest.toString();
            m_updateAvailable = QVersionNumber::compare(latest, current) > 0;
            setStatus(m_updateAvailable
                ? QStringLiteral("Version %1 is available.").arg(m_latestVersion)
                : QStringLiteral("You have the latest version (%1).").arg(m_currentVersion));
        }
        reply->deleteLater();
    });
}

void ReleaseChecker::openReleases() const
{
    QDesktopServices::openUrl(QUrl{QString::fromLatin1(repositoryUrl) + QStringLiteral("/releases/latest")});
}

void ReleaseChecker::openRepository() const
{
    QDesktopServices::openUrl(QUrl{QString::fromLatin1(repositoryUrl)});
}
