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

#include "mdkqthelper.h"
#include "include/mdk/c/MediaInfo.h"
#include "include/mdk/c/Player.h"
#include "include/mdk/c/VideoFrame.h"
#include "include/mdk/c/global.h"
#include <QtCore/qdebug.h>
#include <QtCore/qmutex.h>
#include <QtCore/qlibrary.h>
#include <QtCore/qdir.h>

#ifndef WWX190_GENERATE_MDKAPI
#define WWX190_GENERATE_MDKAPI(funcName, resultType, ...) \
    using _WWX190_MDKAPI_lp_##funcName = resultType(*)(__VA_ARGS__); \
    _WWX190_MDKAPI_lp_##funcName m_lp_##funcName = nullptr;
#endif

#ifndef WWX190_RESOLVE_MDKAPI
#define WWX190_RESOLVE_MDKAPI(funcName) \
    if (!m_lp_##funcName) { \
        qCDebug(QTMEDIAPLAYER_PREPEND_NAMESPACE(lcQMPMDK)) << "Resolving function:" << #funcName; \
        m_lp_##funcName = reinterpret_cast<_WWX190_MDKAPI_lp_##funcName>(m_library.resolve(#funcName)); \
        Q_ASSERT(m_lp_##funcName); \
        if (!m_lp_##funcName) { \
            qCWarning(QTMEDIAPLAYER_PREPEND_NAMESPACE(lcQMPMDK)) << "Failed to resolve function:" << #funcName; \
        } \
    }
#endif

#ifndef WWX190_CALL_MDKAPI
#define WWX190_CALL_MDKAPI(funcName, ...) \
    QMutexLocker locker(&MDK::Qt::mdkData()->m_mutex); \
    if (MDK::Qt::mdkData()->m_lp_##funcName) { \
        MDK::Qt::mdkData()->m_lp_##funcName(__VA_ARGS__); \
    }
#endif

#ifndef WWX190_CALL_MDKAPI_RETURN
#define WWX190_CALL_MDKAPI_RETURN(funcName, defVal, ...) \
    QMutexLocker locker(&MDK::Qt::mdkData()->m_mutex); \
    return (MDK::Qt::mdkData()->m_lp_##funcName ? MDK::Qt::mdkData()->m_lp_##funcName(__VA_ARGS__) : defVal);
#endif

#ifndef WWX190_SETNULL_MDKAPI
#define WWX190_SETNULL_MDKAPI(funcName) \
    if (m_lp_##funcName) { \
        m_lp_##funcName = nullptr; \
    }
#endif

#ifndef WWX190_NOTNULL_MDKAPI
#define WWX190_NOTNULL_MDKAPI(funcName) (m_lp_##funcName != nullptr)
#endif

namespace MDK::Qt
{

static constexpr const char _mdkHelper_mdk_fileName_envVar[] = "QTMEDIAPLAYER_MDK_FILENAME";

struct MDKData
{
    mutable QMutex m_mutex = {};

    // global.h
    WWX190_GENERATE_MDKAPI(MDK_javaVM, void *, void *)
    WWX190_GENERATE_MDKAPI(MDK_setLogLevel, void, MDK_LogLevel)
    WWX190_GENERATE_MDKAPI(MDK_logLevel, MDK_LogLevel)
    WWX190_GENERATE_MDKAPI(MDK_setLogHandler, void, mdkLogHandler)
    WWX190_GENERATE_MDKAPI(MDK_setGlobalOptionString, void, const char *, const char *)
    WWX190_GENERATE_MDKAPI(MDK_setGlobalOptionInt32, void, const char *, int)
    WWX190_GENERATE_MDKAPI(MDK_setGlobalOptionPtr, void, const char *, void *)
    WWX190_GENERATE_MDKAPI(MDK_getGlobalOptionString, bool, const char *, const char **)
    WWX190_GENERATE_MDKAPI(MDK_getGlobalOptionInt32, bool, const char *, int *)
    WWX190_GENERATE_MDKAPI(MDK_getGlobalOptionPtr, bool, const char *, void **)
    WWX190_GENERATE_MDKAPI(MDK_strdup, char *, const char *)
    WWX190_GENERATE_MDKAPI(MDK_version, int)

    // MediaInfo.h
    WWX190_GENERATE_MDKAPI(MDK_AudioStreamCodecParameters, void, const mdkAudioStreamInfo *, mdkAudioCodecParameters *)
    WWX190_GENERATE_MDKAPI(MDK_AudioStreamMetadata, bool, const mdkAudioStreamInfo *, mdkStringMapEntry *)
    WWX190_GENERATE_MDKAPI(MDK_VideoStreamCodecParameters, void, const mdkVideoStreamInfo *, mdkVideoCodecParameters *)
    WWX190_GENERATE_MDKAPI(MDK_VideoStreamMetadata, bool, const mdkVideoStreamInfo *, mdkStringMapEntry *)
    WWX190_GENERATE_MDKAPI(MDK_MediaMetadata, bool, const mdkMediaInfo *, mdkStringMapEntry *)

    // Player.h
    WWX190_GENERATE_MDKAPI(mdkPlayerAPI_new, const mdkPlayerAPI *)
    WWX190_GENERATE_MDKAPI(mdkPlayerAPI_delete, void, const struct mdkPlayerAPI **)
    WWX190_GENERATE_MDKAPI(MDK_foreignGLContextDestroyed, void)

    // VideoFrame.h
    WWX190_GENERATE_MDKAPI(mdkVideoFrameAPI_new, mdkVideoFrameAPI *, int, int, enum MDK_PixelFormat)
    WWX190_GENERATE_MDKAPI(mdkVideoFrameAPI_delete, void, struct mdkVideoFrameAPI **)

    explicit MDKData()
    {
        QStringList candidates = { QStringLiteral("mdk-0"), QStringLiteral("mdk") };
        const QString rawFileNames = qEnvironmentVariable(_mdkHelper_mdk_fileName_envVar);
        if (!rawFileNames.isEmpty()) {
            const QStringList fileNames = rawFileNames.split(u';', ::Qt::SkipEmptyParts, ::Qt::CaseInsensitive);
            if (!fileNames.isEmpty()) {
                candidates << fileNames;
            }
        }
        for (auto &&fileName : qAsConst(candidates)) {
            if (!fileName.isEmpty()) {
                if (load(fileName)) {
                    break;
                }
            }
        }
    }

    ~MDKData()
    {
        const bool result = unload();
        Q_UNUSED(result);
    }

    [[nodiscard]] inline bool load(const QString &path)
    {
        Q_ASSERT(!path.isEmpty());
        if (path.isEmpty()) {
            qCWarning(QTMEDIAPLAYER_PREPEND_NAMESPACE(lcQMPMDK)) << "Failed to load MDK: empty library path.";
            return false;
        }

        if (isLoaded()) {
            qCDebug(QTMEDIAPLAYER_PREPEND_NAMESPACE(lcQMPMDK)) << "MDK already loaded. Unloading ...";
            if (!unload()) {
                return false;
            }
        }

        QMutexLocker locker(&m_mutex);

        m_library.unload(); // Unload first, to clear previous errors.
        m_library.setFileName(path);
        qCDebug(QTMEDIAPLAYER_PREPEND_NAMESPACE(lcQMPMDK)) << "Start loading MDK from:" << QDir::toNativeSeparators(path);
        if (m_library.load()) {
            qCDebug(QTMEDIAPLAYER_PREPEND_NAMESPACE(lcQMPMDK)) << "MDK has been loaded successfully.";
        } else {
            qCWarning(QTMEDIAPLAYER_PREPEND_NAMESPACE(lcQMPMDK)) << "Failed to load MDK:" << m_library.errorString();
            return false;
        }

        // global.h
        WWX190_RESOLVE_MDKAPI(MDK_javaVM)
        WWX190_RESOLVE_MDKAPI(MDK_setLogLevel)
        WWX190_RESOLVE_MDKAPI(MDK_logLevel)
        WWX190_RESOLVE_MDKAPI(MDK_setLogHandler)
        WWX190_RESOLVE_MDKAPI(MDK_setGlobalOptionString)
        WWX190_RESOLVE_MDKAPI(MDK_setGlobalOptionInt32)
        WWX190_RESOLVE_MDKAPI(MDK_setGlobalOptionPtr)
        WWX190_RESOLVE_MDKAPI(MDK_getGlobalOptionString)
        WWX190_RESOLVE_MDKAPI(MDK_getGlobalOptionInt32)
        WWX190_RESOLVE_MDKAPI(MDK_getGlobalOptionPtr)
        WWX190_RESOLVE_MDKAPI(MDK_strdup)
        WWX190_RESOLVE_MDKAPI(MDK_version)

        // MediaInfo.h
        WWX190_RESOLVE_MDKAPI(MDK_AudioStreamCodecParameters)
        WWX190_RESOLVE_MDKAPI(MDK_AudioStreamMetadata)
        WWX190_RESOLVE_MDKAPI(MDK_VideoStreamCodecParameters)
        WWX190_RESOLVE_MDKAPI(MDK_VideoStreamMetadata)
        WWX190_RESOLVE_MDKAPI(MDK_MediaMetadata)

        // Player.h
        WWX190_RESOLVE_MDKAPI(mdkPlayerAPI_new)
        WWX190_RESOLVE_MDKAPI(mdkPlayerAPI_delete)
        WWX190_RESOLVE_MDKAPI(MDK_foreignGLContextDestroyed)

        // VideoFrame.h
        WWX190_RESOLVE_MDKAPI(mdkVideoFrameAPI_new)
        WWX190_RESOLVE_MDKAPI(mdkVideoFrameAPI_delete)

        return true;
    }

    [[nodiscard]] inline bool unload()
    {
        QMutexLocker locker(&m_mutex);

        // global.h
        WWX190_SETNULL_MDKAPI(MDK_javaVM)
        WWX190_SETNULL_MDKAPI(MDK_setLogLevel)
        WWX190_SETNULL_MDKAPI(MDK_logLevel)
        WWX190_SETNULL_MDKAPI(MDK_setLogHandler)
        WWX190_SETNULL_MDKAPI(MDK_setGlobalOptionString)
        WWX190_SETNULL_MDKAPI(MDK_setGlobalOptionInt32)
        WWX190_SETNULL_MDKAPI(MDK_setGlobalOptionPtr)
        WWX190_SETNULL_MDKAPI(MDK_getGlobalOptionString)
        WWX190_SETNULL_MDKAPI(MDK_getGlobalOptionInt32)
        WWX190_SETNULL_MDKAPI(MDK_getGlobalOptionPtr)
        WWX190_SETNULL_MDKAPI(MDK_strdup)
        WWX190_SETNULL_MDKAPI(MDK_version)

        // MediaInfo.h
        WWX190_SETNULL_MDKAPI(MDK_AudioStreamCodecParameters)
        WWX190_SETNULL_MDKAPI(MDK_AudioStreamMetadata)
        WWX190_SETNULL_MDKAPI(MDK_VideoStreamCodecParameters)
        WWX190_SETNULL_MDKAPI(MDK_VideoStreamMetadata)
        WWX190_SETNULL_MDKAPI(MDK_MediaMetadata)

        // Player.h
        WWX190_SETNULL_MDKAPI(mdkPlayerAPI_new)
        WWX190_SETNULL_MDKAPI(mdkPlayerAPI_delete)
        WWX190_SETNULL_MDKAPI(MDK_foreignGLContextDestroyed)

        // VideoFrame.h
        WWX190_SETNULL_MDKAPI(mdkVideoFrameAPI_new)
        WWX190_SETNULL_MDKAPI(mdkVideoFrameAPI_delete)

        if (m_library.isLoaded()) {
            if (!m_library.unload()) {
                qCWarning(QTMEDIAPLAYER_PREPEND_NAMESPACE(lcQMPMDK)) << "Failed to unload MDK:" << m_library.errorString();
                return false;
            }
        }

        qCDebug(QTMEDIAPLAYER_PREPEND_NAMESPACE(lcQMPMDK)) << "MDK unloaded successfully.";
        return true;
    }

    [[nodiscard]] inline bool isLoaded() const
    {
        QMutexLocker locker(&m_mutex);
        const bool result =
                // global.h
                WWX190_NOTNULL_MDKAPI(MDK_javaVM) &&
                WWX190_NOTNULL_MDKAPI(MDK_setLogLevel) &&
                WWX190_NOTNULL_MDKAPI(MDK_logLevel) &&
                WWX190_NOTNULL_MDKAPI(MDK_setLogHandler) &&
                WWX190_NOTNULL_MDKAPI(MDK_setGlobalOptionString) &&
                WWX190_NOTNULL_MDKAPI(MDK_setGlobalOptionInt32) &&
                WWX190_NOTNULL_MDKAPI(MDK_setGlobalOptionPtr) &&
                WWX190_NOTNULL_MDKAPI(MDK_getGlobalOptionString) &&
                WWX190_NOTNULL_MDKAPI(MDK_getGlobalOptionInt32) &&
                WWX190_NOTNULL_MDKAPI(MDK_getGlobalOptionPtr) &&
                WWX190_NOTNULL_MDKAPI(MDK_strdup) &&
                WWX190_NOTNULL_MDKAPI(MDK_version) &&
                // MediaInfo.h
                WWX190_NOTNULL_MDKAPI(MDK_AudioStreamCodecParameters) &&
                WWX190_NOTNULL_MDKAPI(MDK_AudioStreamMetadata) &&
                WWX190_NOTNULL_MDKAPI(MDK_VideoStreamCodecParameters) &&
                WWX190_NOTNULL_MDKAPI(MDK_VideoStreamMetadata) &&
                WWX190_NOTNULL_MDKAPI(MDK_MediaMetadata) &&
                // Player.h
                WWX190_NOTNULL_MDKAPI(mdkPlayerAPI_new) &&
                WWX190_NOTNULL_MDKAPI(mdkPlayerAPI_delete) &&
                WWX190_NOTNULL_MDKAPI(MDK_foreignGLContextDestroyed) &&
                // VideoFrame.h
                WWX190_NOTNULL_MDKAPI(mdkVideoFrameAPI_new) &&
                WWX190_NOTNULL_MDKAPI(mdkVideoFrameAPI_delete);
        return result;
    }

private:
    Q_DISABLE_COPY_MOVE(MDKData)
    QLibrary m_library;
};

Q_GLOBAL_STATIC(MDKData, mdkData)

bool isMDKAvailable()
{
    return mdkData()->isLoaded();
}

QString getMDKVersion()
{
    const int fullVerNum = MDK_version();
    const int majorVerNum = ((fullVerNum >> 16) & 0xff);
    const int minorVerNum = ((fullVerNum >> 8) & 0xff);
    const int patchVerNum = (fullVerNum & 0xff);
    return QStringLiteral("%1.%2.%3").arg(QString::number(majorVerNum),
                                          QString::number(minorVerNum),
                                          QString::number(patchVerNum));
}

} // namespace MDK::Qt

///////////////////////////////////////////
/// MDK
///////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

// global.h

void *MDK_javaVM(void *value)
{
    WWX190_CALL_MDKAPI_RETURN(MDK_javaVM, nullptr, value)
}

void MDK_setLogLevel(MDK_LogLevel value)
{
    WWX190_CALL_MDKAPI(MDK_setLogLevel, value)
}

MDK_LogLevel MDK_logLevel()
{
    WWX190_CALL_MDKAPI_RETURN(MDK_logLevel, MDK_LogLevel_Debug)
}

void MDK_setLogHandler(mdkLogHandler value)
{
    WWX190_CALL_MDKAPI(MDK_setLogHandler, value)
}

void MDK_setGlobalOptionString(const char *key, const char *value)
{
    WWX190_CALL_MDKAPI(MDK_setGlobalOptionString, key, value)
}

void MDK_setGlobalOptionInt32(const char *key, int value)
{
    WWX190_CALL_MDKAPI(MDK_setGlobalOptionInt32, key, value)
}

void MDK_setGlobalOptionPtr(const char *key, void *value)
{
    WWX190_CALL_MDKAPI(MDK_setGlobalOptionPtr, key, value)
}

bool MDK_getGlobalOptionString(const char *key, const char **value)
{
    WWX190_CALL_MDKAPI_RETURN(MDK_getGlobalOptionString, false, key, value)
}

bool MDK_getGlobalOptionInt32(const char *key, int *value)
{
    WWX190_CALL_MDKAPI_RETURN(MDK_getGlobalOptionInt32, false, key, value)
}

bool MDK_getGlobalOptionPtr(const char *key, void **value)
{
    WWX190_CALL_MDKAPI_RETURN(MDK_getGlobalOptionPtr, false, key, value)
}

char *MDK_strdup(const char *value)
{
    WWX190_CALL_MDKAPI_RETURN(MDK_strdup, nullptr, value)
}

int MDK_version()
{
    WWX190_CALL_MDKAPI_RETURN(MDK_version, MDK_VERSION)
}

// MediaInfo.h

void MDK_AudioStreamCodecParameters(const mdkAudioStreamInfo *asi, mdkAudioCodecParameters *acp)
{
    WWX190_CALL_MDKAPI(MDK_AudioStreamCodecParameters, asi, acp)
}

bool MDK_AudioStreamMetadata(const mdkAudioStreamInfo *asi, mdkStringMapEntry *sme)
{
    WWX190_CALL_MDKAPI_RETURN(MDK_AudioStreamMetadata, false, asi, sme)
}

void MDK_VideoStreamCodecParameters(const mdkVideoStreamInfo *vsi, mdkVideoCodecParameters *vcp)
{
    WWX190_CALL_MDKAPI(MDK_VideoStreamCodecParameters, vsi, vcp)
}

bool MDK_VideoStreamMetadata(const mdkVideoStreamInfo *vsi, mdkStringMapEntry *sme)
{
    WWX190_CALL_MDKAPI_RETURN(MDK_VideoStreamMetadata, false, vsi, sme)
}

bool MDK_MediaMetadata(const mdkMediaInfo *mi, mdkStringMapEntry *sme)
{
    WWX190_CALL_MDKAPI_RETURN(MDK_MediaMetadata, false, mi, sme)
}

// Player.h

const mdkPlayerAPI *mdkPlayerAPI_new()
{
    WWX190_CALL_MDKAPI_RETURN(mdkPlayerAPI_new, nullptr)
}

void mdkPlayerAPI_delete(const struct mdkPlayerAPI **value)
{
    WWX190_CALL_MDKAPI(mdkPlayerAPI_delete, value)
}

void MDK_foreignGLContextDestroyed()
{
    WWX190_CALL_MDKAPI(MDK_foreignGLContextDestroyed)
}

// VideoFrame.h

mdkVideoFrameAPI *mdkVideoFrameAPI_new(int w, int h, enum MDK_PixelFormat f)
{
    WWX190_CALL_MDKAPI_RETURN(mdkVideoFrameAPI_new, nullptr, w, h, f)
}

void mdkVideoFrameAPI_delete(struct mdkVideoFrameAPI **value)
{
    WWX190_CALL_MDKAPI(mdkVideoFrameAPI_delete, value)
}

#ifdef __cplusplus
}
#endif
