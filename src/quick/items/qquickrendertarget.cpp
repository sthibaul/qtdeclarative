// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qquickrendertarget_p.h"
#include <rhi/qrhi.h>
#include <QtQuick/private/qquickitem_p.h>
#include <QtQuick/private/qquickwindow_p.h>
#include <QtQuick/private/qsgrhisupport_p.h>

QT_BEGIN_NAMESPACE

/*!
    \class QQuickRenderTarget
    \since 6.0
    \inmodule QtQuick

    \brief The QQuickRenderTarget class provides an opaque container for native
    graphics resources specifying a render target, and associated metadata.

    \sa QQuickWindow::setRenderTarget(), QQuickGraphicsDevice
*/

QQuickRenderTargetPrivate::QQuickRenderTargetPrivate()
    : ref(1)
{
}

QQuickRenderTargetPrivate::QQuickRenderTargetPrivate(const QQuickRenderTargetPrivate &other)
    : ref(1),
      type(other.type),
      pixelSize(other.pixelSize),
      devicePixelRatio(other.devicePixelRatio),
      sampleCount(other.sampleCount),
      u(other.u),
      mirrorVertically(other.mirrorVertically),
      multisampleResolve(other.multisampleResolve)
{
}

/*!
    Constructs a default QQuickRenderTarget that does not reference any native
    objects.
 */
QQuickRenderTarget::QQuickRenderTarget()
    : d(new QQuickRenderTargetPrivate)
{
}

/*!
    \internal
 */
void QQuickRenderTarget::detach()
{
    qAtomicDetach(d);
}

/*!
    \internal
 */
QQuickRenderTarget::QQuickRenderTarget(const QQuickRenderTarget &other)
    : d(other.d)
{
    d->ref.ref();
}

/*!
    \internal
 */
QQuickRenderTarget &QQuickRenderTarget::operator=(const QQuickRenderTarget &other)
{
    qAtomicAssign(d, other.d);
    return *this;
}

/*!
    Destructor.
 */
QQuickRenderTarget::~QQuickRenderTarget()
{
    if (!d->ref.deref())
        delete d;
}

/*!
    \return true if this QQuickRenderTarget is default constructed, referencing
    no native objects.
 */
bool QQuickRenderTarget::isNull() const
{
    return d->type == QQuickRenderTargetPrivate::Type::Null;
}

/*!
    \return the device pixel ratio for the render target. This is the ratio
    between \e{device pixels} and \e{device independent pixels}.

    The default device pixel ratio is 1.0.

    \since 6.3

    \sa setDevicePixelRatio()
*/
qreal QQuickRenderTarget::devicePixelRatio() const
{
    return d->devicePixelRatio;
}

/*!
    Sets the device pixel ratio for this render target to \a ratio. This is
    the ratio between \e{device pixels} and \e{device independent pixels}.

    Note that the specified device pixel ratio value will be ignored if
    QQuickRenderControl::renderWindow() is re-implemented to return a valid
    QWindow.

    \since 6.3

    \sa devicePixelRatio()
*/
void QQuickRenderTarget::setDevicePixelRatio(qreal ratio)
{
    if (d->devicePixelRatio == ratio)
        return;

    detach();
    d->devicePixelRatio = ratio;
}

/*!
    \return Returns whether the render target is mirrored vertically.

    The default value is \c {false}.

    \since 6.4

    \sa setMirrorVertically()
*/
bool QQuickRenderTarget::mirrorVertically() const
{
    return d->mirrorVertically;
}


/*!
    Sets the size of the render target contents should be mirrored vertically to
    \a enable when drawing. This allows easy integration of third-party rendering
    code that does not follow the standard expectations.

    \note This function should not be used when using the \c software backend.

    \since 6.4

    \sa mirrorVertically()
 */
void QQuickRenderTarget::setMirrorVertically(bool enable)
{
    if (d->mirrorVertically == enable)
        return;

    detach();
    d->mirrorVertically = enable;
}

/*!
    \return a new QQuickRenderTarget referencing an OpenGL texture object
    specified by \a textureId.

    \a format specifies the native internal format of the
    texture. Only texture formats that are supported by Qt's rendering
    infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples. 0 or 1 means no
    multisampling, while a value like 4 or 8 states that the native object is a
    multisample texture.

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil buffer, if applicable, is
    created and used automatically.

    The OpenGL object name \a textureId must be a valid name in the rendering
    context used by the Qt Quick scenegraph.

    \note the resulting QQuickRenderTarget does not own any native resources,
    it merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.4

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
 */
#if QT_CONFIG(opengl) || defined(Q_QDOC)
QQuickRenderTarget QQuickRenderTarget::fromOpenGLTexture(uint textureId, uint format,
                                                         const QSize &pixelSize, int sampleCount)
{
    QQuickRenderTarget rt;
    QQuickRenderTargetPrivate *d = QQuickRenderTargetPrivate::get(&rt);

    if (!textureId) {
        qWarning("QQuickRenderTarget: textureId is invalid");
        return rt;
    }

    if (pixelSize.isEmpty()) {
        qWarning("QQuickRenderTarget: Cannot create with empty size");
        return rt;
    }

    d->type = QQuickRenderTargetPrivate::Type::NativeTexture;
    d->pixelSize = pixelSize;
    d->sampleCount = qMax(1, sampleCount);

    QRhiTexture::Flags rhiFlags;
    auto rhiFormat = QSGRhiSupport::toRhiTextureFormatFromGL(format, &rhiFlags);
    d->u.nativeTexture = { textureId, 0, uint(rhiFormat), uint(rhiFlags) };

    return rt;
}

/*!
    \return a new QQuickRenderTarget referencing an OpenGL texture object
    specified by \a textureId.

    Unlike fromOpenGLTexture(), this variant assumes that \a textureId is a
    non-multisample 2D texture, whereas \a sampleCount defines the number of
    samples desired. The resulting QQuickRenderTarget will use an intermediate,
    automatically created multisample texture as its color attachment, and will
    resolve the samples into \a textureId. This is the recommended approach to
    perform MSAA when the native OpenGL texture is not already multisample.

    \a format specifies the native internal format of the texture. Only texture
    formats that are supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples used for multisample
    antialiasing. 0 or 1 means no multisampling, in which case this function is
    identical to fromOpenGLTexture().

    A depth-stencil buffer, if applicable, is created and used automatically.

    The OpenGL object name \a textureId must be a valid name in the rendering
    context used by the Qt Quick scenegraph.

    \note the resulting QQuickRenderTarget does not own any native resources, it
    merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.8

    \note The implementation of this function is not currently compatible with
    OpenGL ES 3.0 and requires OpenGL ES 3.1 at minimum. (or OpenGL 3.0 on desktop)

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl, fromOpenGLTexture()
 */
QQuickRenderTarget QQuickRenderTarget::fromOpenGLTextureWithMultiSampleResolve(uint textureId, uint format, const QSize &pixelSize, int sampleCount)
{
    QQuickRenderTarget rt = fromOpenGLTexture(textureId, format, pixelSize, sampleCount);
    QQuickRenderTargetPrivate::get(&rt)->multisampleResolve = sampleCount > 1;
    return rt;
}

/*!
    \return a new QQuickRenderTarget referencing a 2D texture array with the
    specified \a arraySize and OpenGL \a textureId.

    \note This implies multiview rendering (GL_OVR_multiview etc.), which can be
    relevant with VR/AR especially. \a arraySize is the number of views,
    typically \c 2. This overload should not be used other cases.
    See \l QSGMaterial::viewCount() for details on enabling multiview rendering
    within the Qt Quick scenegraph.

    \a format specifies the native internal format of the texture. Only texture
    formats that are supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels.

    \a sampleCount specifies the number of samples. 0 or 1 means no
    multisampling, while a value like 4 or 8 states that the native object is a
    multisample texture.

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil texture array with a matching
    number of layers, sample count, and a format of \c D24S8 is created and used
    automatically.

    \note the resulting QQuickRenderTarget does not own any native resources, it
    merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.8

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
 */
QQuickRenderTarget QQuickRenderTarget::fromOpenGLTextureMultiView(uint textureId, uint format, const QSize &pixelSize, int sampleCount, int arraySize)
{
    QQuickRenderTarget rt;
    QQuickRenderTargetPrivate *d = QQuickRenderTargetPrivate::get(&rt);

    if (!textureId) {
        qWarning("QQuickRenderTarget: textureId is invalid");
        return rt;
    }

    if (pixelSize.isEmpty()) {
        qWarning("QQuickRenderTarget: Cannot create with empty size");
        return rt;
    }

    if (arraySize < 1) {
        qWarning("QQuickRenderTarget: Texture array must have at least one element");
        return rt;
    }

    d->type = QQuickRenderTargetPrivate::Type::NativeTextureArray;
    d->pixelSize = pixelSize;
    d->sampleCount = qMax(1, sampleCount);

    QRhiTexture::Flags rhiFlags;
    auto rhiFormat = QSGRhiSupport::toRhiTextureFormatFromGL(format, &rhiFlags);
    d->u.nativeTextureArray = { textureId, 0, arraySize, uint(rhiFormat), uint(rhiFlags) };

    return rt;
}

/*!
    \return a new QQuickRenderTarget referencing a 2D texture array with the
    specified \a arraySize and OpenGL \a textureId.

    \note This implies multiview rendering (GL_OVR_multiview etc.), which can be
    relevant with VR/AR especially. \a arraySize is the number of views,
    typically \c 2. This overload should not be used other cases.
    See \l QSGMaterial::viewCount() for details on enabling multiview rendering
    within the Qt Quick scenegraph.

    Unlike fromOpenGLTextureMultiView(), this variant assumes that \a textureId
    is a non-multisample 2D texture array, whereas \a sampleCount defines the
    number of samples desired. The resulting QQuickRenderTarget will use an
    intermediate, automatically created multisample texture array as its color
    attachment, and will resolve the samples into \a textureId. This is the
    recommended approach to perform MSAA when the native OpenGL texture is not
    already multisample.

    \a format specifies the native internal format of the texture. Only texture
    formats that are supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels.

    \a sampleCount specifies the number of samples used for multisample
    antialiasing. 0 or 1 means no multisampling, in which case this function is
    identical to fromOpenGLTextureMultiView().

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil texture array with a matching
    number of layers, sample count, and a format of \c D24S8 is created and used
    automatically.

    \note the resulting QQuickRenderTarget does not own any native resources, it
    merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.8

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
 */
QQuickRenderTarget QQuickRenderTarget::fromOpenGLTextureMultiViewWithMultiSampleResolve(uint textureId, uint format, const QSize &pixelSize, int sampleCount, int arraySize)
{
    QQuickRenderTarget rt = fromOpenGLTextureMultiView(textureId, format, pixelSize, sampleCount, arraySize);
    QQuickRenderTargetPrivate::get(&rt)->multisampleResolve = sampleCount > 1;
    return rt;
}

/*!
    \overload

    \return a new QQuickRenderTarget referencing an OpenGL texture
    object specified by \a textureId. The texture is assumed to have a
    format of GL_RGBA (GL_RGBA8).

    \a pixelSize specifies the size of the image, in pixels. Currently
    only 2D textures are supported.

    \a sampleCount specifies the number of samples. 0 or 1 means no
    multisampling, while a value like 4 or 8 states that the native
    object is a multisample texture.

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil buffer, if applicable, is
    created and used automatically.

    The OpenGL object name \a textureId must be a valid name in the rendering
    context used by the Qt Quick scenegraph.

    \note the resulting QQuickRenderTarget does not own any native resources,
    it merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
*/
QQuickRenderTarget QQuickRenderTarget::fromOpenGLTexture(uint textureId, const QSize &pixelSize, int sampleCount)
{
    return fromOpenGLTexture(textureId, 0, pixelSize, sampleCount);
}

/*!
    \return a new QQuickRenderTarget referencing an OpenGL renderbuffer object
    specified by \a renderbufferId.

    The renderbuffer will be used as the color attachment for the internal
    framebuffer object. This function is provided to allow targeting
    renderbuffers that are created by the application with some external buffer
    underneath, such as an EGLImageKHR. Once the application has called
    \l{https://www.khronos.org/registry/OpenGL/extensions/OES/OES_EGL_image.txt}{glEGLImageTargetRenderbufferStorageOES},
    the renderbuffer can be passed to this function.

    \a pixelSize specifies the size of the image, in pixels.

    \a sampleCount specifies the number of samples. 0 or 1 means no
    multisampling, while a value like 4 or 8 states that the native object is a
    multisample renderbuffer.

    \note the resulting QQuickRenderTarget does not own any native resources,
    it merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.2

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
 */
QQuickRenderTarget QQuickRenderTarget::fromOpenGLRenderBuffer(uint renderbufferId, const QSize &pixelSize, int sampleCount)
{
    QQuickRenderTarget rt;
    QQuickRenderTargetPrivate *d = QQuickRenderTargetPrivate::get(&rt);

    if (!renderbufferId) {
        qWarning("QQuickRenderTarget: renderbufferId is invalid");
        return rt;
    }

    if (pixelSize.isEmpty()) {
        qWarning("QQuickRenderTarget: Cannot create with empty size");
        return rt;
    }

    d->type = QQuickRenderTargetPrivate::Type::NativeRenderbuffer;
    d->pixelSize = pixelSize;
    d->sampleCount = qMax(1, sampleCount);
    d->u.nativeRenderbufferObject = renderbufferId;

    return rt;
}
#endif

/*!
    \return a new QQuickRenderTarget referencing a D3D11 texture object
    specified by \a texture.

    \a format specifies the DXGI_FORMAT of the texture. Only texture formats
    that are supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples. 0 or 1 means no
    multisampling, while a value like 4 or 8 states that the native object is a
    multisample texture.

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil buffer, if applicable, is
    created and used automatically.

    \note the resulting QQuickRenderTarget does not own any native resources,
    it merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.4

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
 */
#if defined(Q_OS_WIN) || defined(Q_QDOC)
QQuickRenderTarget QQuickRenderTarget::fromD3D11Texture(void *texture, uint format,
                                                        const QSize &pixelSize, int sampleCount)
{
    QQuickRenderTarget rt;
    QQuickRenderTargetPrivate *d = QQuickRenderTargetPrivate::get(&rt);

    if (!texture) {
        qWarning("QQuickRenderTarget: texture is null");
        return rt;
    }

    if (pixelSize.isEmpty()) {
        qWarning("QQuickRenderTarget: Cannot create with empty size");
        return rt;
    }

    d->type = QQuickRenderTargetPrivate::Type::NativeTexture;
    d->pixelSize = pixelSize;
    d->sampleCount = qMax(1, sampleCount);

    QRhiTexture::Flags flags;
    auto rhiFormat = QSGRhiSupport::toRhiTextureFormatFromDXGI(format, &flags);
    d->u.nativeTexture = { quint64(texture), 0, uint(rhiFormat), uint(flags) };

    return rt;
}

/*!
    \return a new QQuickRenderTarget referencing a D3D11 texture object
    specified by \a texture.

    Unlike fromD3D11Texture(), this variant assumes that \a texture is a
    non-multisample 2D texture, whereas \a sampleCount defines the number of
    samples desired. The resulting QQuickRenderTarget will use an intermediate,
    automatically created multisample texture as its color attachment, and will
    resolve the samples into \a texture. This is the recommended approach to
    perform MSAA when the native Direct 3D texture is not already multisample.

    \a format specifies the DXGI_FORMAT of the texture. Only texture formats
    that are supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples used for multisample
    antialiasing. 0 or 1 means no multisampling, in which case this function is
    identical to fromD3D11Texture().

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil buffer, if applicable, is
    created and used automatically.

    \note the resulting QQuickRenderTarget does not own any native resources, it
    merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.8

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl, fromD3D11Texture()
 */
QQuickRenderTarget QQuickRenderTarget::fromD3D11TextureWithMultiSampleResolve(void *texture, uint format,
                                                                              const QSize &pixelSize, int sampleCount)
{
    QQuickRenderTarget rt = fromD3D11Texture(texture, format, pixelSize, sampleCount);
    QQuickRenderTargetPrivate::get(&rt)->multisampleResolve = sampleCount > 1;
    return rt;
}

/*!
    \overload

    \return a new QQuickRenderTarget referencing a D3D11 texture
    object specified by \a texture. The texture is assumed to have a
    format of DXGI_FORMAT_R8G8B8A8_UNORM.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples. 0 or 1 means no
    multisampling, while a value like 4 or 8 states that the native object is a
    multisample texture.

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil buffer, if applicable, is
    created and used automatically.

    \note the resulting QQuickRenderTarget does not own any native resources,
    it merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
*/
QQuickRenderTarget QQuickRenderTarget::fromD3D11Texture(void *texture, const QSize &pixelSize, int sampleCount)
{
    return fromD3D11Texture(texture, 0 /* DXGI_FORMAT_UNKNOWN */, pixelSize, sampleCount);
}

/*!
    \return a new QQuickRenderTarget referencing a D3D12 texture object
    specified by \a texture.

    \a resourceState must a valid bitmask with bits from D3D12_RESOURCE_STATES,
    specifying the resource's current state.

    \a format specifies the DXGI_FORMAT of the texture. Only texture formats
    that are supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples. 0 or 1 means no
    multisampling, while a value like 4 or 8 states that the native object is a
    multisample texture.

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil buffer, if applicable, is
    created and used automatically.

    \note the resulting QQuickRenderTarget does not own any native resources,
    it merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.6

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
 */
QQuickRenderTarget QQuickRenderTarget::fromD3D12Texture(void *texture,
                                                        int resourceState,
                                                        uint format,
                                                        const QSize &pixelSize,
                                                        int sampleCount)
{
    QQuickRenderTarget rt;
    QQuickRenderTargetPrivate *d = QQuickRenderTargetPrivate::get(&rt);

    if (!texture) {
        qWarning("QQuickRenderTarget: texture is null");
        return rt;
    }

    if (pixelSize.isEmpty()) {
        qWarning("QQuickRenderTarget: Cannot create with empty size");
        return rt;
    }

    d->type = QQuickRenderTargetPrivate::Type::NativeTexture;
    d->pixelSize = pixelSize;
    d->sampleCount = qMax(1, sampleCount);

    QRhiTexture::Flags flags;
    auto rhiFormat = QSGRhiSupport::toRhiTextureFormatFromDXGI(format, &flags);
    d->u.nativeTexture = { quint64(texture), resourceState, uint(rhiFormat), uint(flags) };

    return rt;
}

/*!
    \return a new QQuickRenderTarget referencing a D3D12 texture object
    specified by \a texture.

    Unlike fromD3D12Texture(), this variant assumes that \a texture is a
    non-multisample 2D texture, whereas \a sampleCount defines the number of
    samples desired. The resulting QQuickRenderTarget will use an intermediate,
    automatically created multisample texture as its color attachment, and will
    resolve the samples into \a texture. This is the recommended approach to
    perform MSAA when the native Direct 3D texture is not already multisample.

    \a resourceState must a valid bitmask with bits from D3D12_RESOURCE_STATES,
    specifying the resource's current state.

    \a format specifies the DXGI_FORMAT of the texture. Only texture formats
    that are supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples used for multisample
    antialiasing. 0 or 1 means no multisampling, in which case this function is
    identical to fromD3D12Texture().

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil buffer, if applicable, is
    created and used automatically.

    \note the resulting QQuickRenderTarget does not own any native resources, it
    merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.8

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl, fromD3D12Texture()
 */
QQuickRenderTarget QQuickRenderTarget::fromD3D12TextureWithMultiSampleResolve(void *texture,
                                                                              int resourceState,
                                                                              uint format,
                                                                              const QSize &pixelSize,
                                                                              int sampleCount)
{
    QQuickRenderTarget rt = fromD3D12Texture(texture, resourceState, format, pixelSize, sampleCount);
    QQuickRenderTargetPrivate::get(&rt)->multisampleResolve = sampleCount > 1;
    return rt;
}

/*!
    \return a new QQuickRenderTarget referencing a D3D12 texture array object
    specified by \a texture. The number of array elements (layers) is given in
    \a arraySize.

    \note This implies multiview rendering (GL_OVR_multiview etc.), which can be
    relevant with VR/AR especially. \a arraySize is the number of views,
    typically \c 2. This overload should not be used other cases.
    See \l QSGMaterial::viewCount() for details on enabling multiview rendering
    within the Qt Quick scenegraph.

    \a resourceState must a valid bitmask with bits from D3D12_RESOURCE_STATES,
    specifying the resource's current state.

    \a format specifies the DXGI_FORMAT of the texture. Only texture formats
    that are supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples. 0 or 1 means no
    multisampling, while a value like 4 or 8 states that the native object is a
    multisample texture.

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil texture array with a matching
    number of layers, sample count, and a format of \l{QRhiTexture::}{D24S8} is
    created and used automatically.

    \note the resulting QQuickRenderTarget does not own any native resources, it
    merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.8

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
 */
QQuickRenderTarget QQuickRenderTarget::fromD3D12TextureMultiView(void *texture,
                                                                 int resourceState,
                                                                 uint format,
                                                                 const QSize &pixelSize,
                                                                 int sampleCount,
                                                                 int arraySize)
{
    QQuickRenderTarget rt;
    QQuickRenderTargetPrivate *d = QQuickRenderTargetPrivate::get(&rt);

    if (!texture) {
        qWarning("QQuickRenderTarget: texture is null");
        return rt;
    }

    if (pixelSize.isEmpty()) {
        qWarning("QQuickRenderTarget: Cannot create with empty size");
        return rt;
    }

    if (arraySize < 1) {
        qWarning("QQuickRenderTarget: Texture array must have at least one element");
        return rt;
    }

    d->type = QQuickRenderTargetPrivate::Type::NativeTextureArray;
    d->pixelSize = pixelSize;
    d->sampleCount = qMax(1, sampleCount);

    QRhiTexture::Flags flags;
    auto rhiFormat = QSGRhiSupport::toRhiTextureFormatFromDXGI(format, &flags);
    d->u.nativeTextureArray = { quint64(texture), resourceState, arraySize, uint(rhiFormat), uint(flags) };

    return rt;
}

/*!
    \return a new QQuickRenderTarget referencing a D3D12 texture array object
    specified by \a texture. The number of array elements (layers) is given in
    \a arraySize.

    \note This implies multiview rendering (GL_OVR_multiview etc.), which can be
    relevant with VR/AR especially. \a arraySize is the number of views,
    typically \c 2. This overload should not be used other cases.
    See \l QSGMaterial::viewCount() for details on enabling multiview rendering
    within the Qt Quick scenegraph.

    Unlike fromD3D12TextureMultiView(), this variant assumes that \a texture is a
    non-multisample 2D texture array, whereas \a sampleCount defines the number
    of samples desired. The resulting QQuickRenderTarget will use an
    intermediate, automatically created multisample texture array as its color
    attachment, and will resolve the samples into \a texture. This is the
    recommended approach to perform MSAA when the native Direct 3D texture is not
    already multisample.

    \a resourceState must a valid bitmask with bits from D3D12_RESOURCE_STATES,
    specifying the resource's current state.

    \a format specifies the DXGI_FORMAT of the texture. Only texture formats
    that are supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples used for multisample
    antialiasing. 0 or 1 means no multisampling, in which case this function is
    identical to fromD3D12TextureMultiView().

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil texture array with a matching
    number of layers, sample count, and a format of \l{QRhiTexture::}{D24S8} is
    created and used automatically.

    \note the resulting QQuickRenderTarget does not own any native resources, it
    merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.8

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
 */
QQuickRenderTarget QQuickRenderTarget::fromD3D12TextureMultiViewWithMultiSampleResolve(void *texture,
                                                                                       int resourceState,
                                                                                       uint format,
                                                                                       const QSize &pixelSize,
                                                                                       int sampleCount,
                                                                                       int arraySize)
{
    QQuickRenderTarget rt = fromD3D12TextureMultiView(texture, resourceState, format, pixelSize, sampleCount, arraySize);
    QQuickRenderTargetPrivate::get(&rt)->multisampleResolve = sampleCount > 1;
    return rt;
}
#endif

/*!
    \return a new QQuickRenderTarget referencing a Metal texture object
    specified by \a texture.

    \a format specifies the MTLPixelFormat of the texture. Only texture formats
    that are supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples. 0 or 1 means no
    multisampling, while a value like 4 or 8 states that the native object is a
    multisample texture.

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil buffer, if applicable, is
    created and used automatically.

    \note the resulting QQuickRenderTarget does not own any native resources,
    it merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.4

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
 */
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS) || defined(Q_QDOC)
QQuickRenderTarget QQuickRenderTarget::fromMetalTexture(MTLTexture *texture, uint format,
                                                        const QSize &pixelSize, int sampleCount)
{
    QQuickRenderTarget rt;
    QQuickRenderTargetPrivate *d = QQuickRenderTargetPrivate::get(&rt);

    if (!texture) {
        qWarning("QQuickRenderTarget: texture is null");
        return rt;
    }

    if (pixelSize.isEmpty()) {
        qWarning("QQuickRenderTarget: Cannot create with empty size");
        return rt;
    }

    d->type = QQuickRenderTargetPrivate::Type::NativeTexture;
    d->pixelSize = pixelSize;
    d->sampleCount = qMax(1, sampleCount);

    QRhiTexture::Flags flags;
    auto rhiFormat = QSGRhiSupport::toRhiTextureFormatFromMetal(format, &flags);
    d->u.nativeTexture = { quint64(texture), 0, uint(rhiFormat), uint(flags) };

    return rt;
}

/*!
    \return a new QQuickRenderTarget referencing a Metal texture object
    specified by \a texture.

    Unlike fromMetalTexture(), this variant assumes that \a texture is a
    non-multisample 2D texture, whereas \a sampleCount defines the number of
    samples desired. The resulting QQuickRenderTarget will use an intermediate,
    automatically created multisample texture as its color attachment, and will
    resolve the samples into \a texture. This is the recommended approach to
    perform MSAA when the native Metal texture is not already multisample.

    \a format specifies the MTLPixelFormat of the texture. Only texture formats
    that are supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples used for multisample
    antialiasing. 0 or 1 means no multisampling, in which case this function is
    identical to fromMetalTexture().

    \a sampleCount specifies the number of samples. 0 or 1 means no
    multisampling, while a value like 4 or 8 states that the native object is a
    multisample texture.

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil buffer, if applicable, is
    created and used automatically.

    \note the resulting QQuickRenderTarget does not own any native resources,
    it merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.8

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl, fromMetalTexture()
 */
QQuickRenderTarget QQuickRenderTarget::fromMetalTextureWithMultiSampleResolve(MTLTexture *texture, uint format,
                                                                              const QSize &pixelSize, int sampleCount)
{
    QQuickRenderTarget rt = fromMetalTexture(texture, format, pixelSize, sampleCount);
    QQuickRenderTargetPrivate::get(&rt)->multisampleResolve = sampleCount > 1;
    return rt;
}

/*!
    \return a new QQuickRenderTarget referencing a Metal texture array object
    with \a arraySize elements specified by \a texture.

    \note This implies multiview rendering (GL_OVR_multiview etc.), which can be
    relevant with VR/AR especially. \a arraySize is the number of views,
    typically \c 2. This overload should not be used other cases.
    See \l QSGMaterial::viewCount() for details on enabling multiview rendering
    within the Qt Quick scenegraph.

    \a format specifies the MTLPixelFormat of the texture. Only texture formats
    that are supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples. 0 or 1 means no
    multisampling, while a value like 4 or 8 states that the native object is a
    multisample texture.

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil texture array with a matching
    number of layers, sample count, and a format of \c D24S8 is created and used
    automatically.

    \note the resulting QQuickRenderTarget does not own any native resources, it
    merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.8

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
 */
QQuickRenderTarget QQuickRenderTarget::fromMetalTextureMultiView(MTLTexture *texture, uint format,
                                                                 const QSize &pixelSize, int sampleCount, int arraySize)
{
    QQuickRenderTarget rt;
    QQuickRenderTargetPrivate *d = QQuickRenderTargetPrivate::get(&rt);

    if (!texture) {
        qWarning("QQuickRenderTarget: texture is null");
        return rt;
    }

    if (pixelSize.isEmpty()) {
        qWarning("QQuickRenderTarget: Cannot create with empty size");
        return rt;
    }

    if (arraySize < 1) {
        qWarning("QQuickRenderTarget: Texture array must have at least one element");
        return rt;
    }

    d->type = QQuickRenderTargetPrivate::Type::NativeTextureArray;
    d->pixelSize = pixelSize;
    d->sampleCount = qMax(1, sampleCount);

    QRhiTexture::Flags flags;
    auto rhiFormat = QSGRhiSupport::toRhiTextureFormatFromMetal(format, &flags);
    d->u.nativeTextureArray = { quint64(texture), 0, arraySize, uint(rhiFormat), uint(flags) };

    return rt;
}

/*!
    \return a new QQuickRenderTarget referencing a Metal texture array object
    with \a arraySize elements specified by \a texture.

    \note This implies multiview rendering (GL_OVR_multiview etc.), which can be
    relevant with VR/AR especially. \a arraySize is the number of views,
    typically \c 2. This overload should not be used other cases.
    See \l QSGMaterial::viewCount() for details on enabling multiview rendering
    within the Qt Quick scenegraph.

    Unlike fromMetalTextureMultiView(), this variant assumes that \a texture is a
    non-multisample 2D texture array, whereas \a sampleCount defines the number
    of samples desired. The resulting QQuickRenderTarget will use an
    intermediate, automatically created multisample texture array as its color
    attachment, and will resolve the samples into \a texture. This is the
    recommended approach to perform MSAA when the native Metal texture is not
    already multisample.

    \a format specifies the MTLPixelFormat of the texture. Only texture formats
    that are supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples used for multisample
    antialiasing. 0 or 1 means no multisampling, in which case this function is
    identical to fromMetalTextureMultiView().

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil texture array with a matching
    number of layers, sample count, and a format of \c D24S8 is created and used
    automatically.

    \note the resulting QQuickRenderTarget does not own any native resources, it
    merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.8

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
 */
QQuickRenderTarget QQuickRenderTarget::fromMetalTextureMultiViewWithMultiSampleResolve(MTLTexture *texture, uint format,
                                                                                       const QSize &pixelSize, int sampleCount, int arraySize)
{
    QQuickRenderTarget rt = fromMetalTextureMultiView(texture, format, pixelSize, sampleCount, arraySize);
    QQuickRenderTargetPrivate::get(&rt)->multisampleResolve = sampleCount > 1;
    return rt;
}

/*!
    \overload

    \return a new QQuickRenderTarget referencing a Metal texture object
    specified by \a texture. The texture is assumed to have a format of
    MTLPixelFormatRGBA8Unorm.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples. 0 or 1 means no
    multisampling, while a value like 4 or 8 states that the native object is a
    multisample texture.

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil buffer, if applicable, is
    created and used automatically.

    \note the resulting QQuickRenderTarget does not own any native resources,
    it merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
*/
QQuickRenderTarget QQuickRenderTarget::fromMetalTexture(MTLTexture *texture, const QSize &pixelSize, int sampleCount)
{
    return fromMetalTexture(texture, 0 /* MTLPixelFormatInvalid */, pixelSize, sampleCount);
}
#endif

/*!
    \return a new QQuickRenderTarget referencing a Vulkan image object
    specified by \a image. The current \a layout of the image must be provided
    as well.

    \a format specifies the VkFormat of the image. Only image formats that are
    supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples. 0 or 1 means no
    multisampling, while a value like 4 or 8 states that the native object is a
    multisample texture.

    The image is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil buffer, if applicable, is
    created and used automatically.

    \note the resulting QQuickRenderTarget does not own any native resources,
    it merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.4

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
 */
#if QT_CONFIG(vulkan) || defined(Q_QDOC)
QQuickRenderTarget QQuickRenderTarget::fromVulkanImage(VkImage image, VkImageLayout layout, VkFormat format,
                                                       const QSize &pixelSize, int sampleCount)
{
    QQuickRenderTarget rt;
    QQuickRenderTargetPrivate *d = QQuickRenderTargetPrivate::get(&rt);

    if (image == VK_NULL_HANDLE) {
        qWarning("QQuickRenderTarget: image is invalid");
        return rt;
    }

    if (pixelSize.isEmpty()) {
        qWarning("QQuickRenderTarget: Cannot create with empty size");
        return rt;
    }

    d->type = QQuickRenderTargetPrivate::Type::NativeTexture;
    d->pixelSize = pixelSize;
    d->sampleCount = qMax(1, sampleCount);

    QRhiTexture::Flags flags;
    auto rhiFormat = QSGRhiSupport::toRhiTextureFormatFromVulkan(format, &flags);
    d->u.nativeTexture = { quint64(image), layout, uint(rhiFormat), uint(flags) };

    return rt;
}

/*!
    \return a new QQuickRenderTarget referencing a Vulkan image object
    specified by \a image. The current \a layout of the image must be provided
    as well.

    Unlike fromVulkanImage(), this variant assumes that \a image is a
    non-multisample 2D texture, whereas \a sampleCount defines the number of
    samples desired. The resulting QQuickRenderTarget will use an intermediate,
    automatically created multisample texture as its color attachment, and will
    resolve the samples into \a image. This is the recommended approach to
    perform MSAA when the native Vulkan image is not already multisample.

    \a format specifies the VkFormat of the image. Only image formats that are
    supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples used for multisample
    antialiasing. 0 or 1 means no multisampling, in which case this function is
    identical to fromVulkanImage().

    The image is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil buffer, if applicable, is
    created and used automatically.

    \note the resulting QQuickRenderTarget does not own any native resources,
    it merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.8

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl, fromVulkanImage()
 */
QQuickRenderTarget QQuickRenderTarget::fromVulkanImageWithMultiSampleResolve(VkImage image, VkImageLayout layout, VkFormat format,
                                                                             const QSize &pixelSize, int sampleCount)
{
    QQuickRenderTarget rt = fromVulkanImage(image, layout, format, pixelSize, sampleCount);
    QQuickRenderTargetPrivate::get(&rt)->multisampleResolve = sampleCount > 1;
    return rt;
}

/*!
    \return a new QQuickRenderTarget referencing a Vulkan image object with
    \a arraySize layers specified by \a image. The current \a layout of the image
    must be provided as well.

    \note This implies multiview rendering (GL_OVR_multiview etc.), which can be
    relevant with VR/AR especially. \a arraySize is the number of views,
    typically \c 2. This overload should not be used other cases.
    See \l QSGMaterial::viewCount() for details on enabling multiview rendering
    within the Qt Quick scenegraph.

    \a format specifies the VkFormat of the image. Only image formats that are
    supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples. 0 or 1 means no
    multisampling, while a value like 4 or 8 states that the native object is a
    multisample texture.

    The image is used as the first color attachment of the render target used by
    the Qt Quick scenegraph. A depth-stencil texture array with a matching
    number of layers, sample count, and a format of \c D24S8 is created and used
    automatically.

    \note the resulting QQuickRenderTarget does not own any native resources, it
    merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.8

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
 */
QQuickRenderTarget QQuickRenderTarget::fromVulkanImageMultiView(VkImage image, VkImageLayout layout, VkFormat format,
                                                                const QSize &pixelSize, int sampleCount, int arraySize)
{
    QQuickRenderTarget rt;
    QQuickRenderTargetPrivate *d = QQuickRenderTargetPrivate::get(&rt);

    if (image == VK_NULL_HANDLE) {
        qWarning("QQuickRenderTarget: image is invalid");
        return rt;
    }

    if (pixelSize.isEmpty()) {
        qWarning("QQuickRenderTarget: Cannot create with empty size");
        return rt;
    }

    if (arraySize < 1) {
        qWarning("QQuickRenderTarget: Texture array must have at least one element");
        return rt;
    }

    d->type = QQuickRenderTargetPrivate::Type::NativeTextureArray;
    d->pixelSize = pixelSize;
    d->sampleCount = qMax(1, sampleCount);

    QRhiTexture::Flags flags;
    auto rhiFormat = QSGRhiSupport::toRhiTextureFormatFromVulkan(format, &flags);
    d->u.nativeTextureArray = { quint64(image), layout, arraySize, uint(rhiFormat), uint(flags) };

    return rt;
}

/*!
    \return a new QQuickRenderTarget referencing a Vulkan image object with
    \a arraySize layers specified by \a image. The current \a layout of the image
    must be provided as well.

    \note This implies multiview rendering (GL_OVR_multiview etc.), which can be
    relevant with VR/AR especially. \a arraySize is the number of views,
    typically \c 2. This overload should not be used other cases.
    See \l QSGMaterial::viewCount() for details on enabling multiview rendering
    within the Qt Quick scenegraph.

    Unlike fromVulkanImageMultiView(), this variant assumes that \a image is a
    non-multisample 2D texture array, whereas \a sampleCount defines the number
    of samples desired. The resulting QQuickRenderTarget will use an
    intermediate, automatically created multisample texture array as its color
    attachment, and will resolve the samples into \a image. This is the
    recommended approach to perform MSAA when the native Vulkan image is not
    already multisample.

    \a format specifies the VkFormat of the image. Only image formats that are
    supported by Qt's rendering infrastructure should be used.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples used for multisample
    antialiasing. 0 or 1 means no multisampling, in which case this function is
    identical to fromVulkanImageMultiView().

    The image is used as the first color attachment of the render target used by
    the Qt Quick scenegraph. A depth-stencil texture array with a matching
    number of layers, sample count, and a format of \c D24S8 is created and used
    automatically.

    \note the resulting QQuickRenderTarget does not own any native resources, it
    merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \since 6.8

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
 */
QQuickRenderTarget QQuickRenderTarget::fromVulkanImageMultiViewWithMultiSampleResolve(VkImage image, VkImageLayout layout, VkFormat format,
                                                                                      const QSize &pixelSize, int sampleCount, int arraySize)
{
    QQuickRenderTarget rt = fromVulkanImageMultiView(image, layout, format, pixelSize, sampleCount, arraySize);
    QQuickRenderTargetPrivate::get(&rt)->multisampleResolve = sampleCount > 1;
    return rt;
}

/*!
    \overload

    \return a new QQuickRenderTarget referencing a Vulkan image object specified
    by \a image. The image is assumed to have a format of
    VK_FORMAT_R8G8B8A8_UNORM.

    \a pixelSize specifies the size of the image, in pixels. Currently only 2D
    textures are supported.

    \a sampleCount specifies the number of samples. 0 or 1 means no
    multisampling, while a value like 4 or 8 states that the native object is a
    multisample texture.

    The texture is used as the first color attachment of the render target used
    by the Qt Quick scenegraph. A depth-stencil buffer, if applicable, is
    created and used automatically.

    \note the resulting QQuickRenderTarget does not own any native resources,
    it merely contains references and the associated metadata of the size and
    sample count. It is the caller's responsibility to ensure that the native
    resource exists as long as necessary.

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
*/
QQuickRenderTarget QQuickRenderTarget::fromVulkanImage(VkImage image, VkImageLayout layout, const QSize &pixelSize, int sampleCount)
{
    return fromVulkanImage(image, layout, VK_FORMAT_UNDEFINED, pixelSize, sampleCount);
}
#endif

/*!
    \return a new QQuickRenderTarget referencing an existing \a renderTarget.

    \a renderTarget will in most cases be a QRhiTextureRenderTarget, which
    allows directing the Qt Quick scene's rendering into a QRhiTexture.

    \note the resulting QQuickRenderTarget does not own \a renderTarget and any
    underlying native resources, it merely contains references and the
    associated metadata of the size and sample count. It is the caller's
    responsibility to ensure that the referenced resources exists as long as
    necessary.

    \since 6.6

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
*/
QQuickRenderTarget QQuickRenderTarget::fromRhiRenderTarget(QRhiRenderTarget *renderTarget)
{
    QQuickRenderTarget rt;
    QQuickRenderTargetPrivate *d = QQuickRenderTargetPrivate::get(&rt);

    if (!renderTarget) {
        qWarning("QQuickRenderTarget: Needs a valid QRhiRenderTarget");
        return rt;
    }

    d->type = QQuickRenderTargetPrivate::Type::RhiRenderTarget;
    d->pixelSize = renderTarget->pixelSize();
    d->sampleCount = renderTarget->sampleCount();
    d->u.rhiRt = renderTarget;

    return rt;
}

/*!
    \return a new QQuickRenderTarget referencing a paint device object
    specified by \a device.

    This option of redirecting rendering to a QPaintDevice is available only
    when running with the \c software backend of Qt Quick.

    \note The QQuickRenderTarget does not take ownship of \a device, it is the
    caller's responsibility to ensure the object exists as long as necessary.

    \since 6.4

    \sa QQuickWindow::setRenderTarget(), QQuickRenderControl
 */
QQuickRenderTarget QQuickRenderTarget::fromPaintDevice(QPaintDevice *device)
{
    QQuickRenderTarget rt;
    QQuickRenderTargetPrivate *d = QQuickRenderTargetPrivate::get(&rt);

    d->type = QQuickRenderTargetPrivate::Type::PaintDevice;
    d->pixelSize = QSize(device->width(), device->height());
    d->u.paintDevice = device;

    return rt;
}

/*!
    \fn bool QQuickRenderTarget::operator==(const QQuickRenderTarget &a, const QQuickRenderTarget &b) noexcept
    \return true if \a a and \a b refer to the same set of native objects and
    have matching associated data (size, sample count).
*/
/*!
    \fn bool QQuickRenderTarget::operator!=(const QQuickRenderTarget &a, const QQuickRenderTarget &b) noexcept

    \return true if \a a and \a b refer to a different set of native objects,
    or the associated data (size, sample count) does not match.
*/

/*!
    \internal
*/
bool QQuickRenderTarget::isEqual(const QQuickRenderTarget &other) const noexcept
{
    if (d->type != other.d->type
            || d->pixelSize != other.d->pixelSize
            || d->devicePixelRatio != other.d->devicePixelRatio
            || d->sampleCount != other.d->sampleCount
            || d->mirrorVertically != other.d->mirrorVertically
            || d->multisampleResolve != other.d->multisampleResolve)
    {
        return false;
    }

    switch (d->type) {
    case QQuickRenderTargetPrivate::Type::Null:
        break;
    case QQuickRenderTargetPrivate::Type::NativeTexture:
        if (d->u.nativeTexture.object != other.d->u.nativeTexture.object
                || d->u.nativeTexture.layoutOrState != other.d->u.nativeTexture.layoutOrState
                || d->u.nativeTexture.rhiFormat != other.d->u.nativeTexture.rhiFormat
                || d->u.nativeTexture.rhiFlags != other.d->u.nativeTexture.rhiFlags)
            return false;
        break;
    case QQuickRenderTargetPrivate::Type::NativeTextureArray:
        if (d->u.nativeTextureArray.object != other.d->u.nativeTextureArray.object
                || d->u.nativeTextureArray.layoutOrState != other.d->u.nativeTextureArray.layoutOrState
                || d->u.nativeTextureArray.arraySize != other.d->u.nativeTextureArray.arraySize
                || d->u.nativeTextureArray.rhiFormat != other.d->u.nativeTextureArray.rhiFormat
                || d->u.nativeTextureArray.rhiFlags != other.d->u.nativeTextureArray.rhiFlags)
            return false;
        break;
    case QQuickRenderTargetPrivate::Type::NativeRenderbuffer:
        if (d->u.nativeRenderbufferObject != other.d->u.nativeRenderbufferObject)
            return false;
        break;
    case QQuickRenderTargetPrivate::Type::RhiRenderTarget:
        if (d->u.rhiRt != other.d->u.rhiRt)
            return false;
        break;
    case QQuickRenderTargetPrivate::Type::PaintDevice:
        if (d->u.paintDevice != other.d->u.paintDevice)
            return false;
        break;
    default:
        break;
    }

    return true;
}

static bool createRhiRenderTargetWithRenderBuffer(QRhiRenderBuffer *renderBuffer,
                                                  const QSize &pixelSize,
                                                  int sampleCount,
                                                  QRhi *rhi,
                                                  QQuickWindowRenderTarget *dst)
{
    sampleCount = QSGRhiSupport::chooseSampleCount(sampleCount, rhi);

    std::unique_ptr<QRhiRenderBuffer> depthStencil;
    if (dst->implicitBuffers.depthStencil) {
        if (dst->implicitBuffers.depthStencil->pixelSize() == pixelSize
            && dst->implicitBuffers.depthStencil->sampleCount() == sampleCount)
        {
            depthStencil.reset(dst->implicitBuffers.depthStencil);
            dst->implicitBuffers.depthStencil = nullptr;
        }
    }
    dst->implicitBuffers.reset(rhi);

    if (!depthStencil) {
        depthStencil.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixelSize, sampleCount));
        depthStencil->setName(QByteArrayLiteral("Depth-stencil buffer for QQuickRenderTarget"));
        if (!depthStencil->create()) {
            qWarning("Failed to build depth-stencil buffer for QQuickRenderTarget");
            return false;
        }
    }

    QRhiColorAttachment colorAttachment(renderBuffer);
    QRhiTextureRenderTargetDescription rtDesc(colorAttachment);
    rtDesc.setDepthStencilBuffer(depthStencil.get());
    std::unique_ptr<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget(rtDesc));
    rt->setName(QByteArrayLiteral("RT for QQuickRenderTarget with renderbuffer"));
    std::unique_ptr<QRhiRenderPassDescriptor> rp(rt->newCompatibleRenderPassDescriptor());
    rt->setRenderPassDescriptor(rp.get());

    if (!rt->create()) {
        qWarning("Failed to build renderbuffer-based render target for QQuickRenderTarget");
        return false;
    }

    dst->rt.renderTarget = rt.release();
    dst->rt.owns = true;
    dst->res.rpDesc = rp.release();
    dst->implicitBuffers.depthStencil = depthStencil.release();

    return true;
}

static bool createRhiRenderTarget(QRhiTexture *texture,
                                  const QSize &pixelSize,
                                  int sampleCount,
                                  bool multisampleResolve,
                                  QRhi *rhi,
                                  QQuickWindowRenderTarget *dst)
{
    sampleCount = QSGRhiSupport::chooseSampleCount(sampleCount, rhi);
    if (sampleCount <= 1)
        multisampleResolve = false;

    std::unique_ptr<QRhiRenderBuffer> depthStencil;
    if (dst->implicitBuffers.depthStencil) {
        if (dst->implicitBuffers.depthStencil->pixelSize() == pixelSize
            && dst->implicitBuffers.depthStencil->sampleCount() == sampleCount)
        {
            depthStencil.reset(dst->implicitBuffers.depthStencil);
            dst->implicitBuffers.depthStencil = nullptr;
        }
    }

    std::unique_ptr<QRhiTexture> colorBuffer;
    QRhiTexture::Flags multisampleTextureFlags;
    if (multisampleResolve) {
        multisampleTextureFlags = QRhiTexture::RenderTarget;
        // Pass in texture->format() as a hint, to not be tied to rgba8. Also keep the srgb flag.
        if (texture->flags().testFlag(QRhiTexture::sRGB))
            multisampleTextureFlags |= QRhiTexture::sRGB;

        if (dst->implicitBuffers.multisampleTexture) {
            if (dst->implicitBuffers.multisampleTexture->pixelSize() == pixelSize
                && dst->implicitBuffers.multisampleTexture->sampleCount() == sampleCount
                && dst->implicitBuffers.multisampleTexture->flags().testFlags(multisampleTextureFlags))
            {
                colorBuffer.reset(dst->implicitBuffers.multisampleTexture);
                dst->implicitBuffers.multisampleTexture = nullptr;
            }
        }
    }

    dst->implicitBuffers.reset(rhi);

    if (!depthStencil) {
        depthStencil.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixelSize, sampleCount));
        depthStencil->setName(QByteArrayLiteral("Depth-stencil buffer for QQuickRenderTarget"));
        if (!depthStencil->create()) {
            qWarning("Failed to build depth-stencil buffer for QQuickRenderTarget");
            return false;
        }
    }

    if (multisampleResolve && !colorBuffer) {
        colorBuffer.reset(rhi->newTexture(texture->format(), pixelSize, sampleCount, multisampleTextureFlags));
        colorBuffer->setName(QByteArrayLiteral("Multisample color buffer for QQuickRenderTarget"));
        if (!colorBuffer->create()) {
            qWarning("Failed to build multisample color buffer for QQuickRenderTarget");
            return false;
        }
    }

    QRhiColorAttachment colorAttachment;
    if (multisampleResolve) {
        colorAttachment.setTexture(colorBuffer.get());
        colorAttachment.setResolveTexture(texture);
    } else {
        colorAttachment.setTexture(texture);
    }
    QRhiTextureRenderTargetDescription rtDesc(colorAttachment);
    rtDesc.setDepthStencilBuffer(depthStencil.get());
    std::unique_ptr<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget(rtDesc));
    rt->setName(QByteArrayLiteral("RT for QQuickRenderTarget"));
    std::unique_ptr<QRhiRenderPassDescriptor> rp(rt->newCompatibleRenderPassDescriptor());
    rt->setRenderPassDescriptor(rp.get());

    if (!rt->create()) {
        qWarning("Failed to build texture render target for QQuickRenderTarget");
        return false;
    }

    dst->rt.renderTarget = rt.release();
    dst->rt.owns = true;
    dst->res.rpDesc = rp.release();
    dst->implicitBuffers.depthStencil = depthStencil.release();
    if (multisampleResolve)
        dst->implicitBuffers.multisampleTexture = colorBuffer.release();

    return true;
}

static bool createRhiRenderTargetMultiView(QRhiTexture *texture,
                                           const QSize &pixelSize,
                                           int arraySize,
                                           int sampleCount,
                                           bool multisampleResolve,
                                           QRhi *rhi,
                                           QQuickWindowRenderTarget *dst)
{
    sampleCount = QSGRhiSupport::chooseSampleCount(sampleCount, rhi);
    if (sampleCount <= 1)
        multisampleResolve = false;

    std::unique_ptr<QRhiTexture> depthStencil;
    if (dst->implicitBuffers.depthStencilTexture) {
        if (dst->implicitBuffers.depthStencilTexture->pixelSize() == pixelSize
            && dst->implicitBuffers.depthStencilTexture->sampleCount() == sampleCount
            && dst->implicitBuffers.depthStencilTexture->arraySize() == arraySize)
        {
            depthStencil.reset(dst->implicitBuffers.depthStencilTexture);
            dst->implicitBuffers.depthStencilTexture = nullptr;
        }
    }

    std::unique_ptr<QRhiTexture> colorBuffer;
    QRhiTexture::Flags multisampleTextureFlags;
    if (multisampleResolve) {
        multisampleTextureFlags = QRhiTexture::RenderTarget;
        if (texture->flags().testFlag(QRhiTexture::sRGB))
            multisampleTextureFlags |= QRhiTexture::sRGB;

        if (dst->implicitBuffers.multisampleTexture) {
            if (dst->implicitBuffers.multisampleTexture->pixelSize() == pixelSize
                && dst->implicitBuffers.multisampleTexture->sampleCount() == sampleCount
                && dst->implicitBuffers.multisampleTexture->arraySize() == arraySize
                && dst->implicitBuffers.multisampleTexture->flags().testFlags(multisampleTextureFlags))
            {
                colorBuffer.reset(dst->implicitBuffers.multisampleTexture);
                dst->implicitBuffers.multisampleTexture = nullptr;
            }
        }
    }

    dst->implicitBuffers.reset(rhi);

    if (!depthStencil) {
        depthStencil.reset(rhi->newTextureArray(QRhiTexture::D24S8, arraySize, pixelSize, sampleCount, QRhiTexture::RenderTarget));
        depthStencil->setName(QByteArrayLiteral("Depth-stencil buffer (multiview) for QQuickRenderTarget"));
        if (!depthStencil->create()) {
            qWarning("Failed to build depth-stencil texture array for QQuickRenderTarget");
            return false;
        }
    }

    if (multisampleResolve && !colorBuffer) {
        colorBuffer.reset(rhi->newTextureArray(texture->format(), arraySize, pixelSize, sampleCount, multisampleTextureFlags));
        colorBuffer->setName(QByteArrayLiteral("Multisample color buffer (multiview) for QQuickRenderTarget"));
        if (!colorBuffer->create()) {
            qWarning("Failed to build multisample texture array for QQuickRenderTarget");
            return false;
        }
    }

    QRhiColorAttachment colorAttachment;
    colorAttachment.setMultiViewCount(arraySize);
    if (multisampleResolve) {
        colorAttachment.setTexture(colorBuffer.get());
        colorAttachment.setResolveTexture(texture);
    } else {
        colorAttachment.setTexture(texture);
    }

    QRhiTextureRenderTargetDescription rtDesc(colorAttachment);
    rtDesc.setDepthTexture(depthStencil.get());
    std::unique_ptr<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget(rtDesc));
    rt->setName(QByteArrayLiteral("RT for multiview QQuickRenderTarget"));
    std::unique_ptr<QRhiRenderPassDescriptor> rp(rt->newCompatibleRenderPassDescriptor());
    rt->setRenderPassDescriptor(rp.get());

    if (!rt->create()) {
        qWarning("Failed to build multiview texture render target for QQuickRenderTarget");
        return false;
    }

    dst->rt.renderTarget = rt.release();
    dst->rt.owns = true;
    dst->res.rpDesc = rp.release();
    dst->implicitBuffers.depthStencilTexture = depthStencil.release();
    if (multisampleResolve)
        dst->implicitBuffers.multisampleTexture = colorBuffer.release();

    dst->rt.multiViewCount = arraySize;

    return true;
}

bool QQuickRenderTargetPrivate::resolve(QRhi *rhi, QQuickWindowRenderTarget *dst)
{
    // dst->implicitBuffers may contain valid objects. If so, and their
    // properties are suitable, they are expected to be reused. Once taken what
    // we can reuse, it needs to be reset().

    switch (type) {
    case Type::Null:
        dst->implicitBuffers.reset(rhi);
        return true;

    case Type::NativeTexture:
    {
        const auto format = u.nativeTexture.rhiFormat == QRhiTexture::UnknownFormat ? QRhiTexture::RGBA8
                                                                                    : QRhiTexture::Format(u.nativeTexture.rhiFormat);
        const auto flags = QRhiTexture::RenderTarget | QRhiTexture::Flags(u.nativeTexture.rhiFlags);
        std::unique_ptr<QRhiTexture> texture(rhi->newTexture(format, pixelSize, multisampleResolve ? 1 : sampleCount, flags));
        if (!texture->createFrom({ u.nativeTexture.object, u.nativeTexture.layoutOrState })) {
            qWarning("Failed to build wrapper texture for QQuickRenderTarget");
            return false;
        }
        if (!createRhiRenderTarget(texture.get(), pixelSize, sampleCount, multisampleResolve, rhi, dst))
            return false;
        dst->res.texture = texture.release();
    }
        return true;

    case Type::NativeTextureArray:
    {
        const auto format = u.nativeTextureArray.rhiFormat == QRhiTexture::UnknownFormat ? QRhiTexture::RGBA8
                                                                                         : QRhiTexture::Format(u.nativeTextureArray.rhiFormat);
        const auto flags = QRhiTexture::RenderTarget | QRhiTexture::Flags(u.nativeTextureArray.rhiFlags);
        const int arraySize = u.nativeTextureArray.arraySize;
        std::unique_ptr<QRhiTexture> texture(rhi->newTextureArray(format, arraySize, pixelSize, multisampleResolve ? 1 : sampleCount, flags));
        if (!texture->createFrom({ u.nativeTextureArray.object, u.nativeTextureArray.layoutOrState })) {
            qWarning("Failed to build wrapper texture array for QQuickRenderTarget");
            return false;
        }
        if (!createRhiRenderTargetMultiView(texture.get(), pixelSize, arraySize, sampleCount, multisampleResolve, rhi, dst))
             return false;
        dst->res.texture = texture.release();
    }
        return true;

    case Type::NativeRenderbuffer:
    {
        std::unique_ptr<QRhiRenderBuffer> renderbuffer(rhi->newRenderBuffer(QRhiRenderBuffer::Color, pixelSize, sampleCount));
        if (!renderbuffer->createFrom({ u.nativeRenderbufferObject })) {
            qWarning("Failed to build wrapper renderbuffer for QQuickRenderTarget");
            return false;
        }
        if (!createRhiRenderTargetWithRenderBuffer(renderbuffer.get(), pixelSize, sampleCount, rhi, dst))
            return false;
        dst->res.renderBuffer = renderbuffer.release();
    }
        return true;

    case Type::RhiRenderTarget:
        dst->implicitBuffers.reset(rhi);
        dst->rt.renderTarget = u.rhiRt;
        dst->rt.owns = false;
        if (dst->rt.renderTarget->resourceType() == QRhiResource::TextureRenderTarget) {
            auto texRt = static_cast<QRhiTextureRenderTarget *>(dst->rt.renderTarget);
            const QRhiTextureRenderTargetDescription desc = texRt->description();
            bool first = true;
            for (auto it = desc.cbeginColorAttachments(), end = desc.cendColorAttachments(); it != end; ++it) {
                if (it->multiViewCount() <= 1)
                    continue;
                if (first || dst->rt.multiViewCount == it->multiViewCount()) {
                    first = false;
                    if (it->texture() && it->texture()->flags().testFlag(QRhiTexture::TextureArray)) {
                        if (it->texture()->arraySize() >= it->layer() + it->multiViewCount()) {
                            dst->rt.multiViewCount = it->multiViewCount();
                        } else {
                            qWarning("Invalid QQuickRenderTarget; needs at least %d elements in texture array, got %d",
                                     it->layer() + it->multiViewCount(),
                                     it->texture()->arraySize());
                            return false;
                        }
                    } else {
                        qWarning("Invalid QQuickRenderTarget; multiview requires a texture array");
                        return false;
                    }
                } else {
                    qWarning("Inconsistent multiViewCount in QQuickRenderTarget (was %d, now found an attachment with %d)",
                             dst->rt.multiViewCount, it->multiViewCount());
                    return false;
                }
            }
        }
        return true;

    case Type::PaintDevice:
        dst->implicitBuffers.reset(rhi);
        dst->sw.paintDevice = u.paintDevice;
        dst->sw.owns = false;
        return true;
    }

    Q_UNREACHABLE_RETURN(false);
}

QT_END_NAMESPACE
