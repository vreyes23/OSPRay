#include "QOSPRayWindow.h"
#include <QtGui>

QOSPRayWindow::QOSPRayWindow(OSPRenderer renderer) : renderingEnabled_(false), frameBuffer_(NULL), renderer_(NULL), camera_(NULL)
{
    // assign renderer
    if(!renderer)
        throw std::runtime_error("must be constructed with an existing renderer");

    renderer_ = renderer;

    // setup camera
    camera_ = ospNewCamera("perspective");

    if(!camera_)
        throw std::runtime_error("could not create camera type 'perspective'");

    ospCommit(camera_);

    ospSetParam(renderer_, "camera", camera_);
}

QOSPRayWindow::~QOSPRayWindow()
{
    // free the frame buffer and camera
    // we don't own the renderer!
    if(frameBuffer_)
    {
        ospFreeFrameBuffer(frameBuffer_);
    }

    if(camera_)
    {
        ospRelease(camera_);
    }
}

void QOSPRayWindow::setRenderingEnabled(bool renderingEnabled)
{
    renderingEnabled_ = renderingEnabled;

    // trigger render if true
    if(renderingEnabled_ == true)
    {
        updateGL();
    }
}

void QOSPRayWindow::setWorldBounds(const osp::box3f &worldBounds)
{
    worldBounds_ = worldBounds;

    // set viewport look at point to center of world bounds
    viewport_.at = center(worldBounds_);

    // set viewport from point relative to center of world bounds
    viewport_.from = viewport_.at - 2.f * viewport_.frame.l.vy;

    updateGL();
}

OSPFrameBuffer QOSPRayWindow::getFrameBuffer()
{
    return frameBuffer_;
}

void QOSPRayWindow::paintGL()
{
    if(!renderingEnabled_ || !frameBuffer_ || !renderer_)
    {
        return;
    }

    // update OSPRay camera if viewport has been modified
    if(viewport_.modified)
    {
        ospSetVec3f(camera_,"pos" ,viewport_.from);
        ospSetVec3f(camera_,"dir" ,viewport_.at - viewport_.from);
        ospSetVec3f(camera_,"up", viewport_.up);
        ospSetf(camera_,"aspect", viewport_.aspect);
        ospSetf(camera_,"fovy", viewport_.fovY);

        ospCommit(camera_);

        viewport_.modified = false;
    }

    ospRenderFrame(frameBuffer_, renderer_);

    uint32 * mappedFrameBuffer = (unsigned int *)ospMapFrameBuffer(frameBuffer_);

    glDrawPixels(windowSize_.x, windowSize_.y, GL_RGBA, GL_UNSIGNED_BYTE, mappedFrameBuffer);

    ospUnmapFrameBuffer(mappedFrameBuffer, frameBuffer_);
}

void QOSPRayWindow::resizeGL(int width, int height)
{
    windowSize_ = osp::vec2i(width, height);

    // reallocate OSPRay framebuffer for new size
    if(frameBuffer_)
    {
        ospFreeFrameBuffer(frameBuffer_);
    }

    frameBuffer_ = ospNewFrameBuffer(windowSize_, OSP_RGBA_I8);

    // update viewport aspect ratio
    viewport_.aspect = float(width) / float(height);
    viewport_.modified = true;

    // update OpenGL viewport and force redraw
    glViewport(0, 0, width, height);
    updateGL();
}

void QOSPRayWindow::mousePressEvent(QMouseEvent * event)
{
    lastMousePosition_ = event->pos();
}

void QOSPRayWindow::mouseReleaseEvent(QMouseEvent * event)
{
    lastMousePosition_ = event->pos();
}

void QOSPRayWindow::mouseMoveEvent(QMouseEvent * event)
{
    int dx = event->x() - lastMousePosition_.x();
    int dy = event->y() - lastMousePosition_.y();

    if(event->buttons() & Qt::LeftButton)
    {
        // camera rotation about center point
        const float rotationSpeed = 0.003f;

        float du = dx * rotationSpeed;
        float dv = dy * rotationSpeed;

        const osp::vec3f pivot = center(worldBounds_);

        osp::affine3f xfm = osp::affine3f::translate(pivot)
                          * osp::affine3f::rotate(viewport_.frame.l.vx, -dv)
                          * osp::affine3f::rotate(viewport_.frame.l.vz, -du)
                          * osp::affine3f::translate(-pivot);

        viewport_.frame = xfm * viewport_.frame;
        viewport_.from  = xfmPoint(xfm, viewport_.from);
        viewport_.at    = xfmPoint(xfm, viewport_.at);
        viewport_.snapUp();

        viewport_.modified = true;
    }
    else if(event->buttons() & Qt::RightButton)
    {
        // camera distance from center point
        const float motionSpeed = 0.012f;

        float forward = dy * motionSpeed;
        float oldDistance = length(viewport_.at - viewport_.from);
        float newDistance = oldDistance - forward;

        if(newDistance < 1e-3f)
            return;

        viewport_.from = viewport_.at - newDistance * viewport_.frame.l.vy;
        viewport_.frame.p = viewport_.from;

        viewport_.modified = true;
    }

    lastMousePosition_ = event->pos();

    updateGL();
}