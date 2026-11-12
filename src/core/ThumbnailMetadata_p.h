#pragma once

#include <QMetaType>
#include <QString>
#include <functional>

// Private worker/test seam. A snapshot is an observation, not a live file
// handle: storage can change again after the lookup returns.
struct ThumbnailMetadata {
    QString path;
    QString identity;
    QString revision;
    bool valid() const { return !revision.isEmpty(); }
    bool operator==(const ThumbnailMetadata &) const = default;
};
Q_DECLARE_METATYPE(ThumbnailMetadata)

using ThumbnailMetadataLookup = std::function<ThumbnailMetadata(const QString &localPath)>;

// Blocking filesystem work. Only the thumbnail worker calls this in production.
ThumbnailMetadata lookupThumbnailMetadata(const QString &localPath);
