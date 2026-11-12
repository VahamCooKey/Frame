#include "core/MpvRenderItem.h"

#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickWindow>
#include <QQuickOpenGLUtils>
#include <mpv/render_gl.h>
#include <atomic>
#include <utility>

// Qt may delete the GUI item before releasing its scene-graph renderer.
// Keep callback state alive until both are gone. Guard the receiver through
// invokeMethod so a callback cannot enqueue against a partly destroyed item.
struct MpvRenderState {
    QMutex mutex;
    MpvRenderItem *item{nullptr};
    bool updateQueued{false}; // protected by mutex, including GUI delivery
    std::atomic_bool ready{false};
    std::atomic_bool videoRendered{false};
};

namespace {

void mpvUpdateCallback(void *ctx)
{
    auto *state = static_cast<MpvRenderState *>(ctx);
    QMutexLocker lock(&state->mutex);
    if (auto *item = state->item; item && !state->updateQueued) {
        state->updateQueued = true;
        // The receiver owns this state until its destructor removes queued
        // calls. Clear before update: a racing callback must queue the next
        // request even while this one is being delivered.
        if (!QMetaObject::invokeMethod(item, [item, state] {
                {
                    QMutexLocker lock(&state->mutex);
                    state->updateQueued = false;
                }
                item->update();
            }, Qt::QueuedConnection))
            state->updateQueued = false;
    }
}

void *getProcAddress(void *, const char *name)
{
    auto *ctx = QOpenGLContext::currentContext();
    if (!ctx)
        return nullptr;
    return reinterpret_cast<void *>(ctx->getProcAddress(QByteArray(name)));
}

} // namespace

class MpvRenderer final : public QObject, public QQuickFramebufferObject::Renderer {
public:
    explicit MpvRenderer(std::shared_ptr<MpvRenderState> state)
        : m_state(std::move(state))
    {
    }

    ~MpvRenderer() override
    {
        m_state->ready = false;
        m_state->videoRendered = false;
        disconnect(m_swapConnection);
        if (m_mpvGl) {
            mpv_render_context_set_update_callback(m_mpvGl, nullptr, nullptr);
            mpv_render_context_free(m_mpvGl);
        }
    }

    void synchronize(QQuickFramebufferObject *fbo) override
    {
        auto *item = static_cast<MpvRenderItem *>(fbo);
        m_handle = item->player() ? item->player()->handle() : nullptr;
        // The connection belongs to the renderer, not the GUI item. Both
        // swap reports and context cleanup execute on the render thread.
        if (m_window != item->window()) {
            disconnect(m_swapConnection);
            m_window = item->window();
            m_swapConnection = connect(m_window, &QQuickWindow::frameSwapped, this, [this] {
                if (m_mpvGl)
                    mpv_render_context_report_swap(m_mpvGl);
                if (m_pendingPresentation != 0) {
                    const auto generation = m_pendingPresentation;
                    QMutexLocker lock(&m_state->mutex);
                    if (auto *item = m_state->item)
                        QMetaObject::invokeMethod(item, [item, generation, state = m_state] {
                            auto *player = item->player();
                            auto *window = item->window();
                            if (player && window && window->isVisible() && window->isExposed()
                                && player->presentationGeneration() == generation
                                && player->presentationFrameReady()) {
                                state->videoRendered = true;
                                player->videoFramePresented(generation);
                            }
                        }, Qt::QueuedConnection);
                }
            }, Qt::DirectConnection);
        }
        if (auto *player = item->player()) {
            const auto generation = player->presentationGeneration();
            if (generation != m_generation) {
                m_generation = generation;
                m_firstVideoLogged = false;
                m_pendingPresentation = 0;
                m_state->videoRendered = false;
            }
            m_frameReady = player->presentationFrameReady();
            if (!m_frameReady) m_pendingPresentation = 0;
        }
    }

    void render() override
    {
        if (!m_mpvGl && m_handle) {
            mpv_opengl_init_params glInit{};
            glInit.get_proc_address = getProcAddress;
            mpv_render_param params[] = {
                {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
                {MPV_RENDER_PARAM_INVALID, nullptr},
            };
            if (mpv_render_context_create(&m_mpvGl, m_handle, params) >= 0) {
                m_state->ready = true;
                mpv_render_context_set_update_callback(m_mpvGl, mpvUpdateCallback, m_state.get());
                {
                    QMutexLocker lock(&m_state->mutex);
                    if (auto *item = m_state->item)
                        QMetaObject::invokeMethod(item, [item] { emit item->renderContextReady(); },
                                                  Qt::QueuedConnection);
                }
                qInfo("mpvrender: render context created");
            } else {
                m_mpvGl = nullptr;
                qWarning("mpvrender: mpv_render_context_create FAILED");
            }
        }

        if (m_mpvGl) {
            auto *gl = QOpenGLContext::currentContext()->functions();
            GLint viewport[4] = {0, 0, 0, 0};
            gl->glGetIntegerv(GL_VIEWPORT, viewport);
            auto *qtFbo = framebufferObject();
            mpv_opengl_fbo fbo{qtFbo ? static_cast<int>(qtFbo->handle()) : 0,
                               viewport[2], viewport[3], 0};
            // QQuickFramebufferObject's FBO is already in the engine's expected
            // orientation; flipping here mirrors the image vertically.
            int flipY = 0;
            mpv_render_param params[] = {
                {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
                {MPV_RENDER_PARAM_FLIP_Y, &flipY},
                {MPV_RENDER_PARAM_INVALID, nullptr},
            };
            const int renderRet = mpv_render_context_render(m_mpvGl, params);
            // PLAYBACK_RESTART (m_frameReady) follows the VO's first flip, so
            // from then on mpv holds this file's frame and any render draws
            // it. Whether that frame is still pending or not must not matter:
            // with the window hidden nothing renders, mpv gives up waiting
            // ("mpv_render_context_render() not being called or stuck") and
            // keeps the frame as its current one, so the first render after
            // the window reappears presents real video with no next frame.
            if (!m_firstVideoLogged && renderRet >= 0 && m_frameReady) {
                m_firstVideoLogged = true;
                m_pendingPresentation = m_generation;
                qInfo("mpvrender: first video frame rendered");
            }
            if (!m_firstRenderLogged || renderRet < 0) {
                if (!m_firstRenderLogged)
                    qInfo("mpvrender: first render ret=%d fbo=%d %dx%d", renderRet,
                          qtFbo ? static_cast<int>(qtFbo->handle()) : 0,
                          viewport[2], viewport[3]);
                else
                    qWarning("mpvrender: mpv_render_context_render ret=%d", renderRet);
                m_firstRenderLogged = true;
            }
        } else {
            if (auto *gl = QOpenGLContext::currentContext() ? QOpenGLContext::currentContext()->functions() : nullptr) {
                gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                gl->glClear(GL_COLOR_BUFFER_BIT);
            }
        }
        QQuickOpenGLUtils::resetOpenGLState();
    }

private:
    std::shared_ptr<MpvRenderState> m_state;
    mpv_handle *m_handle{nullptr};
    QQuickWindow *m_window{nullptr};
    QMetaObject::Connection m_swapConnection;
    mpv_render_context *m_mpvGl{nullptr};
    bool m_firstRenderLogged{false};
    bool m_firstVideoLogged{false};
    bool m_frameReady{false};
    quint64 m_generation{0};
    quint64 m_pendingPresentation{0};
};

MpvRenderItem::MpvRenderItem(QQuickItem *parent)
    : QQuickFramebufferObject(parent), m_renderState(std::make_shared<MpvRenderState>())
{
    m_renderState->item = this;
}

MpvRenderItem::~MpvRenderItem()
{
    QMutexLocker lock(&m_renderState->mutex);
    m_renderState->item = nullptr;
}

QQuickFramebufferObject::Renderer *MpvRenderItem::createRenderer() const
{
    return new MpvRenderer(m_renderState);
}

void MpvRenderItem::setPlayer(PlayerController *player)
{
    if (m_player.loadRelaxed() == player)
        return;
    if (auto *old = m_player.loadRelaxed()) disconnect(old, nullptr, this, nullptr);
    m_player.storeRelaxed(player);
    if (player) {
        player->requireVideoPresentation();
        connect(player, &PlayerController::presentationStateChanged, this, &MpvRenderItem::update);
    }
    emit playerChanged();
    update();
}

bool MpvRenderItem::renderReady() const
{
    return m_renderState->ready;
}

bool MpvRenderItem::hasRenderedVideo() const
{
    return m_renderState->videoRendered;
}
