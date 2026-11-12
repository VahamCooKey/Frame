#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

// A session choice, independent of the IDs assigned by each media file.
class TrackPreference {
public:
    enum class Mode { Unset, Off, Selected };

    Mode mode() const { return m_mode; }
    bool remember(const QVariantList &tracks, const QString &type, int id);
    // -1 means no sufficiently identifiable match; the caller uses Auto.
    int matchingId(const QVariantList &tracks, const QString &type) const;

private:
    Mode m_mode{Mode::Unset};
    QVariantMap m_identity;
    QVariantList m_layout;
    int m_ordinal{-1};
};
