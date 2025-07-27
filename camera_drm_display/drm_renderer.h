#ifndef DRM_RENDERER_H
#define DRM_RENDERER_H

#include <string>
#include <vector>
#include <memory> // For std::unique_ptr

// Forward declarations for DRM/GBM types
// These are opaque pointers, actual definitions are in xf86drm.h, xf86drmMode.h, gbm.h
struct drm_mode_modeinfo;
struct drm_mode_crtc;
struct drm_mode_connector;
struct gbm_device;
struct gbm_surface;
struct gbm_bo;

namespace libcamera {
    class PixelFormat; // Forward declaration for libcamera::PixelFormat
}

// Structure to hold information about a DRM framebuffer
struct DrmFb {
    uint32_t id;        // DRM framebuffer ID
    uint32_t width;
    uint32_t height;
    uint32_t pitch;     // Stride of the buffer
    uint32_t handle;    // GEM handle
    uint32_t size;      // Size of the buffer in bytes
    void *map_ptr;      // Mapped CPU pointer to the buffer
    std::unique_ptr<gbm_bo, void (*)(gbm_bo *)> bo; // GBM buffer object
};

class DrmRenderer {
public:
    DrmRenderer();
    ~DrmRenderer();

    // Initializes the DRM renderer: opens device, finds display, sets mode, allocates buffers.
    bool init(int width, int height);

    // Renders a frame to the DRM framebuffer.
    // y_data, u_data, v_data: Pointers to Y, U, V planes of the libcamera frame.
    // y_stride, u_stride, v_stride: Strides for each plane.
    // format: libcamera pixel format (e.g., YUV420).
    void render_frame(const void *y_data, const void *u_data, const void *v_data,
                      int y_stride, int u_stride, int v_stride,
                      int width, int height, const libcamera::PixelFormat &format);

    // Cleans up all DRM/GBM resources.
    void cleanup();

    // Added: Public method to get the DRM file descriptor for polling
    int getDrmFd() const { return drm_fd_; }

    // Moved to public: Callback for DRM page flip events
    static void page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec,
                                  unsigned int tv_usec, void *user_data);

private:
    // Helper functions for DRM setup
    bool find_display_resources();
    bool setup_gbm_buffers(int width, int height);
    void destroy_gbm_buffers();
    void yuv420_to_xrgb8888(const void *y_data, const void *u_data, const void *v_data,
                            int y_stride, int u_stride, int v_stride,
                            int width, int height, uint8_t *rgb_buffer, int rgb_stride);

    int drm_fd_ = -1; // DRM device file descriptor
    gbm_device *gbm_device_ = nullptr; // GBM device

    drm_mode_crtc *crtc_ = nullptr;      // Selected CRTC
    drm_mode_connector *connector_ = nullptr; // Selected connector
    drm_mode_modeinfo *mode_ = nullptr;     // Selected display mode

    std::vector<DrmFb> framebuffers_; // Vector of DRM framebuffers (for double buffering)
    int current_fb_idx_ = 0; // Index of the currently active framebuffer
};

#endif // DRM_RENDERER_H
