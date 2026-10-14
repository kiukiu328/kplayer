#pragma once

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickOpenGLUtils>

namespace MpvOpenGL {
inline void resetState()
{
    QQuickOpenGLUtils::resetOpenGLState();
    auto *context = QOpenGLContext::currentContext();
    if (!context)
        return;
    auto *gl = context->functions();

    // libmpv expects default pixel-transfer state. Qt's reset does not cover
    // these settings, which can corrupt video planes and high-bit-depth colors.
    gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (!context->isOpenGLES()) {
        gl->glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE);
        gl->glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
        gl->glPixelStorei(GL_PACK_LSB_FIRST, GL_FALSE);
        gl->glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE);
    }
    if (!context->isOpenGLES() || context->format().majorVersion() >= 3) {
        gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        gl->glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        gl->glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        gl->glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        gl->glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        gl->glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
        gl->glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
    } else if (context->hasExtension(QByteArrayLiteral("GL_EXT_unpack_subimage"))) {
        gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        gl->glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        gl->glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    }
}
}
