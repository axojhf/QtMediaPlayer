/*
 * MIT License
 *
 * Copyright (C) 2022 by wangwenx190 (Yuhang Zhao)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "mpvvideotexturenode.h"
#include "mpvplayer.h"
#include "mpvqthelper.h"
#include "include/mpv/render_gl.h"
#include <QtCore/qdebug.h>
#include <QtGui/qscreen.h>
#include <QtGui/qopenglcontext.h>
#include <QtQuick/qquickwindow.h>
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QtOpenGL/qopenglframebufferobject.h>
#else
#include <QtGui/qopenglframebufferobject.h>
#endif
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QtQuick/qquickopenglutils.h>
#endif
#if (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID))
#include <QtGui/qguiapplication.h>
#include <QtGui/qpa/qplatformnativeinterface.h>
#include <xcb/xcb.h>
#endif

QTMEDIAPLAYER_BEGIN_NAMESPACE

#if (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID))
using Display = struct _XDisplay;

[[maybe_unused]] [[nodiscard]] static inline Display *x11_display()
{
    if (!qApp) {
        return nullptr;
    }
    QPlatformNativeInterface *native = qApp->platformNativeInterface();
    if (!native) {
        return nullptr;
    }
    void *display = native->nativeResourceForIntegration(QByteArrayLiteral("display"));
    return reinterpret_cast<Display *>(display);
}
#endif

[[nodiscard]] static inline void *get_proc_address_mpv(void *ctx, const char *name)
{
    Q_UNUSED(ctx);
    Q_ASSERT(name);
    if (!name) {
        return nullptr;
    }
    const QOpenGLContext * const glctx = QOpenGLContext::currentContext();
    return (glctx ? reinterpret_cast<void *>(glctx->getProcAddress(name)) : nullptr);
}

static inline void on_mpv_redraw(void *ctx)
{
    Q_ASSERT(ctx);
    if (!ctx) {
        return;
    }
    MPVPlayer::on_update(ctx);
}

MPVVideoTextureNode::MPVVideoTextureNode(QQuickItem *item) : VideoTextureNode(item)
{
    Q_ASSERT(item);
    if (!item) {
        qFatal("null mpv player item.");
    }
    m_item = static_cast<MPVPlayer *>(item);
    m_window = m_item->window();
    connect(m_window, &QQuickWindow::beforeRendering, this, &MPVVideoTextureNode::render);
    connect(m_window, &QQuickWindow::screenChanged, this, [this](QScreen *screen){
        Q_UNUSED(screen);
        m_item->update();
    });
}

MPVVideoTextureNode::~MPVVideoTextureNode()
{
    const auto tex = texture();
    if (tex) {
        delete tex;
    }
    qCDebug(lcQMPMPV) << "Renderer destroyed.";
}

void MPVVideoTextureNode::sync()
{
    Q_ASSERT(m_item);
    Q_ASSERT(m_window);
    if (!m_item || !m_window) {
        return;
    }
    // effectiveDevicePixelRatio() will always give the correct result even if QQuickWindow is not available.
    const auto dpr = m_window->effectiveDevicePixelRatio();
#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
    const QSize newSize = QSizeF(m_item->size() * dpr).toSize();
#else
    const QSize newSize = {qRound(m_item->width() * dpr), qRound(m_item->height() * dpr)};
#endif
    if (texture() && (newSize == m_size)) {
        return;
    }
    Q_ASSERT(m_item->m_mpv);
    if (!m_item->m_mpv) {
        return;
    }
    m_size = newSize;
    const auto tex = ensureTexture(nullptr, m_size);
    if (!tex) {
        return;
    }
    delete texture();
    setTexture(tex);
    // MUST set when texture() is available
    setTextureCoordinatesTransform(TextureCoordinatesTransformFlag::NoTransform);
    setFiltering(QSGTexture::Linear);
    // Qt's own API will apply correct DPR automatically. Don't double scale.
    setRect(0, 0, m_item->width(), m_item->height());
}

// This is hooked up to beforeRendering() so we can start our own render
// command encoder. If we instead wanted to use the scenegraph's render command
// encoder (targeting the window), it should be connected to
// beforeRenderPassRecording() instead.
void MPVVideoTextureNode::render()
{
    Q_ASSERT(m_item);
    Q_ASSERT(m_window);
    if (!m_item || !m_window) {
        return;
    }

    Q_ASSERT(m_item->m_mpv);
    Q_ASSERT(m_item->m_mpv_gl);
    if (!m_item->m_mpv || !m_item->m_mpv_gl) {
        return;
    }

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QQuickOpenGLUtils::resetOpenGLState();
#else
    m_window->resetOpenGLState();
#endif

    mpv_opengl_fbo mpvFBO = {};
    mpvFBO.fbo = static_cast<int>(fbo_gl->handle());
    mpvFBO.w = fbo_gl->width();
    mpvFBO.h = fbo_gl->height();
    mpvFBO.internal_format = 0;

    mpv_render_param params[] =
    {
        // Specify the default framebuffer (0) as target. This will
        // render onto the entire screen. If you want to show the video
        // in a smaller rectangle or apply fancy transformations, you'll
        // need to render into a separate FBO and draw it manually.
        {
            MPV_RENDER_PARAM_OPENGL_FBO,
            &mpvFBO
        },
        {
            MPV_RENDER_PARAM_INVALID,
            nullptr
        }
    };
    // See render_gl.h on what OpenGL environment mpv expects, and
    // other API details.
    mpv_render_context_render(m_item->m_mpv_gl, params);

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QQuickOpenGLUtils::resetOpenGLState();
#else
    m_window->resetOpenGLState();
#endif
}

QSGTexture* MPVVideoTextureNode::ensureTexture(void *player, const QSize &size)
{
    Q_UNUSED(player);

    Q_ASSERT(m_item);
    Q_ASSERT(m_window);
    if (!m_item || !m_window) {
        return nullptr;
    }

    Q_ASSERT(m_item->m_mpv);
    if (!m_item->m_mpv) {
        return nullptr;
    }

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    intmax_t nativeObj = 0;
    int nativeLayout = 0; // Only usable in Vulkan.
#endif // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    const QSGRendererInterface *rif = m_window->rendererInterface();
    switch (rif->graphicsApi()) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    case QSGRendererInterface::OpenGLRhi: // Equal to OpenGL in Qt6
#endif // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    case QSGRendererInterface::OpenGL:
#endif // (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    {
#if QT_CONFIG(opengl)
        fbo_gl.reset(new QOpenGLFramebufferObject(size));
        if (!m_item->m_mpv_gl)
        {
            mpv_opengl_init_params gl_init_params =
            {
                get_proc_address_mpv,
                nullptr
            };
            mpv_render_param display =
            {
                MPV_RENDER_PARAM_INVALID,
                nullptr
            };
#if (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID))
            if (QGuiApplication::platformName().contains(QStringLiteral("xcb"))) {
                display.type = MPV_RENDER_PARAM_X11_DISPLAY;
                display.data = x11_display();
            }
#endif // defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
            mpv_render_param params[] =
            {
                {
                    MPV_RENDER_PARAM_API_TYPE,
                    const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)
                },
                {
                    MPV_RENDER_PARAM_OPENGL_INIT_PARAMS,
                    &gl_init_params
                },
                display,
                {
                    MPV_RENDER_PARAM_INVALID,
                    nullptr
                }
            };

            if (mpv_render_context_create(&m_item->m_mpv_gl, m_item->m_mpv, params) < 0) {
                qFatal("failed to initialize mpv GL context");
            }
            mpv_render_context_set_update_callback(m_item->m_mpv_gl, on_mpv_redraw, m_item);

            // If you try to play any media before this signal is emitted, libmpv will create
            // a separate window to display it instead of rendering in our own QQuickItem.
            QMetaObject::invokeMethod(m_item, "setRendererReady", Q_ARG(bool, true));
        }
        const auto tex = fbo_gl->texture();
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        nativeObj = static_cast<decltype(nativeObj)>(tex);
#if (QT_VERSION <= QT_VERSION_CHECK(5, 14, 0))
        return m_window->createTextureFromId(tex, size);
#endif // (QT_VERSION <= QT_VERSION_CHECK(5, 14, 0))
#else // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        if (tex) {
            return QNativeInterface::QSGOpenGLTexture::fromNative(tex, m_window, size);
        }
#endif // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
#else // QT_CONFIG(opengl)
        qFatal("Rebuild Qt with OpenGL support!");
#endif // QT_CONFIG(opengl)
    } break;
    case QSGRendererInterface::Software:
    {
        // TODO: libmpv also supports software as VO, implement this.
        qFatal("TO BE IMPLEMENTED: Software backend of libmpv.");
    } break;
    default:
        qFatal("Unsupported backend of libmpv: %d", static_cast<int>(rif->graphicsApi()));
        break;
    }
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    if (nativeObj) {
        return m_window->createTextureFromNativeObject(QQuickWindow::NativeObjectTexture, &nativeObj, nativeLayout, size);
    }
#endif // (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
#endif // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    return nullptr;
}

QTMEDIAPLAYER_END_NAMESPACE
