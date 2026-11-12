#pragma once

#include "core/PosterImageCache.h"
#include <QQuickImageProvider>
#include <memory>

// Substitute this for main.cpp's independent PosterImageProvider. The shared
// store outlives either owner and requestImage is safe on Qt's image thread.
class ThumbnailPosterProvider final : public QQuickImageProvider {
public:
    explicit ThumbnailPosterProvider(std::shared_ptr<PosterImageCache> cache)
        : QQuickImageProvider(QQuickImageProvider::Image), m_cache(std::move(cache)) {}

    QImage requestImage(const QString &key, QSize *size, const QSize &) override
    {
        const QImage image = m_cache->image(key);
        if (size)
            *size = image.size();
        return image;
    }

private:
    std::shared_ptr<PosterImageCache> m_cache;
};
