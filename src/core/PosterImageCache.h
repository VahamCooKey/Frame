#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <utility>

// The generator and image provider share this store. Only metadata crosses
// into QML; eviction therefore has one owner and one byte/count budget.
class PosterImageCache final {
public:
    static constexpr qsizetype MaximumBytes = 8 * 1024 * 1024;
    static constexpr int MaximumEntries = 32;
    static constexpr qsizetype MaximumImageBytes = 512 * 512 * 4;

    explicit PosterImageCache(qsizetype maximumBytes = MaximumBytes,
                              int maximumEntries = MaximumEntries)
        : m_maximumBytes(qMax(qsizetype(0), maximumBytes)),
          m_maximumEntries(qMax(0, maximumEntries)) {}

    QImage image(const QString &key) const
    {
        const QMutexLocker lock(&m_mutex);
        const auto it = m_images.constFind(key);
        if (it == m_images.cend())
            return {};
        m_order.removeAll(key);
        m_order.append(key);
        return it->image;
    }

    bool contains(const QString &key) const
    {
        const QMutexLocker lock(&m_mutex);
        return m_images.contains(key);
    }

    QStringList removeOtherRevisions(const QString &identity, const QString &key)
    {
        const QMutexLocker lock(&m_mutex);
        QStringList evicted;
        for (const QString &candidate : std::as_const(m_order)) {
            const Entry &entry = m_images[candidate];
            if (candidate != key && entry.identity == identity)
                evicted.append(candidate);
        }
        for (const QString &candidate : evicted)
            remove(candidate);
        return evicted;
    }

    QStringList insert(const QString &key, const QString &identity, const QImage &image)
    {
        const QMutexLocker lock(&m_mutex);
        QStringList evicted;
        if (image.isNull() || image.width() > 512 || image.height() > 512
            || image.sizeInBytes() > MaximumImageBytes
            || image.sizeInBytes() > m_maximumBytes || m_maximumEntries == 0)
            return evicted;
        remove(key);
        while (!m_order.isEmpty() && (m_images.size() >= m_maximumEntries
               || m_bytes + image.sizeInBytes() > m_maximumBytes)) {
            const QString oldest = m_order.first();
            evicted.append(oldest);
            remove(oldest);
        }
        m_images.insert(key, Entry{identity, image});
        m_order.append(key);
        m_bytes += image.sizeInBytes();
        return evicted;
    }

    qsizetype byteSize() const { const QMutexLocker lock(&m_mutex); return m_bytes; }
    int count() const { const QMutexLocker lock(&m_mutex); return int(m_images.size()); }

private:
    struct Entry { QString identity; QImage image; };
    // Requires m_mutex. Keep this separate from public methods to avoid
    // recursive locking when a single insertion evicts several images.
    void remove(const QString &key)
    {
        const auto it = m_images.find(key);
        if (it == m_images.end())
            return;
        m_bytes -= it->image.sizeInBytes();
        m_images.erase(it);
        m_order.removeAll(key);
    }

    const qsizetype m_maximumBytes;
    const int m_maximumEntries;
    mutable QMutex m_mutex;
    QHash<QString, Entry> m_images;
    mutable QList<QString> m_order;
    qsizetype m_bytes{0};
};
