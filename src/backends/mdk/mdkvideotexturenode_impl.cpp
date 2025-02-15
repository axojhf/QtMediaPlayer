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

#include "mdkvideotexturenode.h"
#include "mdkplayer.h"
#include <cstring>
#include <QtQuick/qquickwindow.h>

#ifdef Q_OS_WINDOWS
#include <d3d11.h>
#include <wrl/client.h>
#endif

#ifdef Q_OS_MACOS
#include <Metal/Metal.h>
#endif

#if QT_CONFIG(opengl)
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QtOpenGL/qopenglframebufferobject.h>
#else
#include <QtGui/qopenglframebufferobject.h>
#endif
#endif

#if (QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>))
#include <QtGui/qvulkaninstance.h>
#include <QtGui/qvulkanfunctions.h>
#endif

// MDK headers must be placed under these graphic headers.
#include "include/mdk/Player.h"
#include "include/mdk/RenderAPI.h"

#if (QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>))
#define VK_ENSURE(x, ...) VK_RUN_CHECK(x, return __VA_ARGS__)
#define VK_WARN(x, ...) VK_RUN_CHECK(x)
#define VK_RUN_CHECK(x, ...) \
    do { \
        VkResult __vkret__ = x; \
        if (__vkret__ != VK_SUCCESS) { \
            qCDebug(lcQMPMDK) << #x " ERROR: " << __vkret__ << " @" << __LINE__ << __func__; \
            __VA_ARGS__; \
        } \
    } while (false)
#endif

QTMEDIAPLAYER_BEGIN_NAMESPACE

class MDKVideoTextureNodeImpl final : public MDKVideoTextureNode
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MDKVideoTextureNodeImpl)

public:
    explicit MDKVideoTextureNodeImpl(QQuickItem *item) : MDKVideoTextureNode(item)
    {
        Q_ASSERT(item);
        if (!item) {
            qFatal("null mdk player item.");
        }
    }

    ~MDKVideoTextureNodeImpl() override
    {
        // Release gfx resources
#if QT_CONFIG(opengl)
        fbo_gl.reset();
#endif
#if (QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>))
        freeTexture();
#endif
    }

protected:
    QSGTexture *ensureTexture(void *player, const QSize &size) override;

private:
#if (QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>))
    bool buildTexture(const QSize &size);
    void freeTexture();
#endif

private:
#if QT_CONFIG(opengl)
    QScopedPointer<QOpenGLFramebufferObject> fbo_gl;
#endif
#ifdef Q_OS_WINDOWS
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_texture_d3d11 = nullptr;
#endif
#ifdef Q_OS_MACOS
    id<MTLTexture> m_texture_mtl = nil;
#endif
#if (QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>))
    VkImage m_texture_vk = VK_NULL_HANDLE;
    VkDeviceMemory m_textureMemory = VK_NULL_HANDLE;
    VkPhysicalDevice m_physDev = VK_NULL_HANDLE;
    VkDevice m_dev = VK_NULL_HANDLE;
    QVulkanDeviceFunctions *m_devFuncs = nullptr;
#endif
};

[[nodiscard]] MDKVideoTextureNode *createNode(MDKPlayer *item)
{
    Q_ASSERT(item);
    if (!item) {
        return nullptr;
    }
    return new MDKVideoTextureNodeImpl(item);
}

QSGTexture *MDKVideoTextureNodeImpl::ensureTexture(void *player, const QSize &size)
{
    Q_ASSERT(player);
    Q_ASSERT(m_window);
    if (!player || !m_window) {
        return nullptr;
    }

    const QSGRendererInterface *rif = m_window->rendererInterface();
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    intmax_t nativeObj = 0;
    int nativeLayout = 0;
#endif // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    switch (rif->graphicsApi()) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    case QSGRendererInterface::OpenGLRhi: // Equal to QSGRendererInterface::OpenGL in Qt6.
#endif // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    case QSGRendererInterface::OpenGL:
#endif // (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    {
#if QT_CONFIG(opengl)
        m_transformMode = TextureCoordinatesTransformFlag::MirrorVertically;
        fbo_gl.reset(new QOpenGLFramebufferObject(size));
        MDK_NS_PREPEND(GLRenderAPI) ra = {};
        ra.fbo = fbo_gl->handle();
        static_cast<MDK_NS_PREPEND(Player) *>(player)->setRenderAPI(&ra, this);
        QMetaObject::invokeMethod(m_item, "setRendererReady", Q_ARG(bool, true));
        const auto tex = fbo_gl->texture();
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        nativeObj = static_cast<decltype(nativeObj)>(tex);
#if (QT_VERSION <= QT_VERSION_CHECK(5, 14, 0))
        return m_window->createTextureFromId(tex, size);
#endif // (QT_VERSION <= QT_VERSION_CHECK(5, 14, 0))
#else // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        if (tex) {
            return QNativeInterface::QSGOpenGLTexture::fromNative(tex, m_window, size, QQuickWindow::TextureHasAlphaChannel);
        }
#endif // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
#else // QT_CONFIG(opengl)
        qFatal("Rebuild Qt with OpenGL support!");
#endif // QT_CONFIG(opengl)
    } break;
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    case QSGRendererInterface::Direct3D11:
#else // (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    case QSGRendererInterface::Direct3D11Rhi: // Equal to QSGRendererInterface::Direct3D11 in Qt6.
#endif // (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    {
#ifdef Q_OS_WINDOWS
        const auto dev = static_cast<ID3D11Device *>(rif->getResource(m_window, QSGRendererInterface::DeviceResource));
        if (!dev) {
            qCCritical(lcQMPMDK) << "Failed to acquire D3D11 device resource.";
            return nullptr;
        }
        const auto desc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R8G8B8A8_UNORM, size.width(), size.height(), 1, 1,
                                             D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
                                             D3D11_USAGE_DEFAULT, 0, 1, 0, 0);
        if (FAILED(dev->CreateTexture2D(&desc, nullptr, &m_texture_d3d11))) {
            qCCritical(lcQMPMDK) << "Failed to create D3D11 2D texture.";
            return nullptr;
        }
        MDK_NS_PREPEND(D3D11RenderAPI) ra = {};
        ra.rtv = m_texture_d3d11.Get();
        static_cast<MDK_NS_PREPEND(Player) *>(player)->setRenderAPI(&ra, this);
        QMetaObject::invokeMethod(m_item, "setRendererReady", Q_ARG(bool, true));
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        nativeObj = reinterpret_cast<decltype(nativeObj)>(m_texture_d3d11.Get());
#else // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        if (m_texture_d3d11) {
            return QNativeInterface::QSGD3D11Texture::fromNative(m_texture_d3d11.Get(), m_window, size, QQuickWindow::TextureHasAlphaChannel);
        }
#endif // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
#else // defined(Q_OS_WINDOWS)
        qFatal("The Direct3D11 backend is only supported on Windows!");
#endif // defined(Q_OS_WINDOWS)
    } break;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    case QSGRendererInterface::Metal:
#else // (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    case QSGRendererInterface::MetalRhi: // Equal to QSGRendererInterface::Metal in Qt6.
#endif // (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    {
#ifdef Q_OS_MACOS
        auto dev = (__bridge id<MTLDevice>)rif->getResource(m_window, QSGRendererInterface::DeviceResource);
        Q_ASSERT(dev);

        MTLTextureDescriptor *desc = [[MTLTextureDescriptor alloc] init];
        desc.textureType = MTLTextureType2D;
        desc.pixelFormat = MTLPixelFormatRGBA8Unorm;
        desc.width = size.width();
        desc.height = size.height();
        desc.mipmapLevelCount = 1;
        desc.resourceOptions = MTLResourceStorageModePrivate;
        desc.storageMode = MTLStorageModePrivate;
        desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        m_texture_mtl = [dev newTextureWithDescriptor: desc];
        MDK_NS_PREPEND(MetalRenderAPI) ra = {};
        ra.texture = (__bridge void*)m_texture_mtl;
        ra.device = (__bridge void*)dev;
        ra.cmdQueue = rif->getResource(m_window, QSGRendererInterface::CommandQueueResource);
        static_cast<MDK_NS_PREPEND(Player) *>(player)->setRenderAPI(&ra, this);
        QMetaObject::invokeMethod(m_item, "setRendererReady", Q_ARG(bool, true));
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        nativeObj = decltype(nativeObj)(ra.texture);
#else // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        if (m_texture_mtl) {
            return QNativeInterface::QSGMetalTexture::fromNative(m_texture_mtl, m_window, size, QQuickWindow::TextureHasAlphaChannel);
        }
#endif // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
#else // defined(Q_OS_MACOS)
        qFatal("The Metal backend is only supported on macOS!");
#endif // defined(Q_OS_MACOS)
    } break;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    case QSGRendererInterface::Vulkan:
#else // (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    case QSGRendererInterface::VulkanRhi: // Equal to QSGRendererInterface::Vulkan in Qt6.
#endif // (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    {
#if (QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>))
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        nativeLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
#endif // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        const auto inst = reinterpret_cast<QVulkanInstance *>(rif->getResource(m_window, QSGRendererInterface::VulkanInstanceResource));
        m_physDev = *static_cast<VkPhysicalDevice *>(rif->getResource(m_window, QSGRendererInterface::PhysicalDeviceResource));
        const auto newDev = *static_cast<VkDevice *>(rif->getResource(m_window, QSGRendererInterface::DeviceResource));
        // TODO: why m_dev is 0 if device lost
        freeTexture();
        m_dev = newDev;
        m_devFuncs = inst->deviceFunctions(m_dev);

        buildTexture(size);
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        nativeObj = reinterpret_cast<decltype(nativeObj)>(m_texture_vk);
#endif // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))

        MDK_NS_PREPEND(VulkanRenderAPI) ra = {};
        ra.device = m_dev;
        ra.phy_device = m_physDev;
        ra.opaque = this;
        ra.rt = m_texture_vk;
        ra.renderTargetInfo = [](void *opaque, int *w, int *h, VkFormat *fmt, VkImageLayout *layout) {
            const auto node = static_cast<MDKVideoTextureNodeImpl *>(opaque);
            *w = node->m_size.width();
            *h = node->m_size.height();
            *fmt = VK_FORMAT_R8G8B8A8_UNORM;
            *layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            return 1;
        };
        ra.currentCommandBuffer = [](void *opaque){
            const auto node = static_cast<MDKVideoTextureNodeImpl *>(opaque);
            const QSGRendererInterface *rif = node->m_window->rendererInterface();
            const auto cmdBuf = *static_cast<VkCommandBuffer *>(rif->getResource(node->m_window, QSGRendererInterface::CommandListResource));
            return cmdBuf;
        };
        static_cast<MDK_NS_PREPEND(Player) *>(player)->setRenderAPI(&ra, this);
        QMetaObject::invokeMethod(m_item, "setRendererReady", Q_ARG(bool, true));
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        if (m_texture_vk) {
            return QNativeInterface::QSGVulkanTexture::fromNative(m_texture_vk, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_window, size, QQuickWindow::TextureHasAlphaChannel);
        }
#endif // (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#else // QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
        qFatal("Rebuild Qt with Vulkan support!");
#endif // QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
    } break;
#endif // (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    case QSGRendererInterface::Software:
    {
        // TODO: implement this.
        qFatal("TO BE IMPLEMENTED: Software backend of MDK.");
    } break;
    default:
        qFatal("Unsupported backend of MDK: %d", static_cast<int>(rif->graphicsApi()));
        break;
    }
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    if (nativeObj) {
        return m_window->createTextureFromNativeObject(QQuickWindow::NativeObjectTexture, &nativeObj, nativeLayout, size, QQuickWindow::TextureHasAlphaChannel);
    }
#endif // (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
#endif // (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    return nullptr;
}

#if (QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>))
bool MDKVideoTextureNodeImpl::buildTexture(const QSize &size)
{
    VkImageCreateInfo imageInfo;
    std::memset(&imageInfo, 0, sizeof(imageInfo));
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = 0;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM; // Qt Quick hardcoded
    imageInfo.extent.width = static_cast<uint32_t>(size.width());
    imageInfo.extent.height = static_cast<uint32_t>(size.height());
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImage image = VK_NULL_HANDLE;
    VK_ENSURE(m_devFuncs->vkCreateImage(m_dev, &imageInfo, nullptr, &image), false);

    m_texture_vk = image;

    VkMemoryRequirements memReq;
    m_devFuncs->vkGetImageMemoryRequirements(m_dev, image, &memReq);

    quint32 memIndex = 0;
    VkPhysicalDeviceMemoryProperties physDevMemProps;
    m_window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(m_physDev, &physDevMemProps);
    for (uint32_t i = 0; i != physDevMemProps.memoryTypeCount; ++i) {
        if (!(memReq.memoryTypeBits & (1 << i))) {
            continue;
        }
        memIndex = i;
    }

    VkMemoryAllocateInfo allocInfo = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        nullptr,
        memReq.size,
        memIndex
    };

    VK_ENSURE(m_devFuncs->vkAllocateMemory(m_dev, &allocInfo, nullptr, &m_textureMemory), false);
    VK_ENSURE(m_devFuncs->vkBindImageMemory(m_dev, image, m_textureMemory, 0), false);

    return true;
}

void MDKVideoTextureNodeImpl::freeTexture()
{
    if (!m_texture_vk) {
        return;
    }
    VK_WARN(m_devFuncs->vkDeviceWaitIdle(m_dev));
    m_devFuncs->vkFreeMemory(m_dev, m_textureMemory, nullptr);
    m_textureMemory = VK_NULL_HANDLE;
    m_devFuncs->vkDestroyImage(m_dev, m_texture_vk, nullptr);
    m_texture_vk = VK_NULL_HANDLE;
}
#endif

QTMEDIAPLAYER_END_NAMESPACE

#include "mdkvideotexturenode_impl.moc"
