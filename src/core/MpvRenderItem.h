// CONTRACT v4 — owned by lead. Amend via CONTRACTS.md only.
// v4: A58 — coalesced render callbacks retain the guarded receiver lifetime.
// v3: A42 — callback state outlives the GUI item; render-context ownership
// stays on the scene-graph thread. Portable playback diagnostics added.
// v2: +renderContextReady() (A17) — emitted once a mpv render context exists,
//     so QML can defer the first loadfile until vo_libmpv can consume frames
//     (a loadfile that wins the race leaves mpv at "No render context set"
//     and the file plays audio-only forever).
#pragma once
#include <QQuickFramebufferObject>
#include <QAtomicPointer>
#include <memory>
#include <mpv/client.h>
#include <mpv/render.h>
#include "PlayerController.h"

struct MpvRenderState;

class MpvRenderItem : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(PlayerController *player READ player WRITE setPlayer NOTIFY playerChanged)

public:
    explicit MpvRenderItem(QQuickItem *parent = nullptr);
    ~MpvRenderItem() override;
    Renderer *createRenderer() const override;

    PlayerController *player() const { return m_player.loadRelaxed(); }
    void setPlayer(PlayerController *p);
    bool renderReady() const;
    bool hasRenderedVideo() const;

signals:
    void playerChanged();
    void requestRedraw();
    // Delivered on the GUI thread after the renderer creates its context.
    void renderContextReady();

private:
    // Written by setPlayer() on the GUI thread, read by the Renderer in
    // synchronize()/render() on the scene-graph thread; atomic keeps the
    // cross-thread read defined instead of a benign-but-UB pointer race.
    QAtomicPointer<PlayerController> m_player{nullptr};
    std::shared_ptr<MpvRenderState> m_renderState;
};
