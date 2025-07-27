#include "drm_renderer.h"

#include <libcamera/formats.h> // For libcamera::formats::YUV420
#include <iostream>
#include <stdexcept>
#include <string.h> // For memcpy
#include <errno.h>  // For errno
#include <sys/mman.h> // For mmap, munmap
#include <algorithm> // For std::max, std::min

// DRM/GBM includes
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>

// Helper macro for error checking
#define DRM_CHECK(x, msg) \
    do { \
        if (!(x)) { \
            std::cerr << "DRM Error: " << msg << " (" << strerror(errno) << ")" << std::endl; \
            return false; \
        } \
    } while (0)

DrmRenderer::DrmRenderer()
    : drm_fd_(-1), gbm_device_(nullptr), crtc_(nullptr), connector_(nullptr), mode_(nullptr), current_fb_idx_(0) {
}

DrmRenderer::~DrmRenderer() {
    cleanup();
}

bool DrmRenderer::init(int width, int height) {
    // 1. Open DRM device
    drm_fd_ = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    DRM_CHECK(drm_fd_ >= 0, "Failed to open DRM device /dev/dri/card0");
    std::cout << "Opened DRM device: " << drm_fd_ << std::endl;

    // 2. Authenticate DRM
    // drmIsMaster is deprecated; newer systems might use cap_sys_admin or similar.
    // For simplicity, we'll keep this check for now, but be aware it might not be strictly necessary
    // or might fail on systems where you have appropriate permissions via other means.
    // DRM_CHECK(drmIsMaster(drm_fd_) == 0, "Not DRM master or failed to authenticate");
    // std::cout << "DRM authenticated." << std::endl;

    // 3. Find display resources (CRTC, connector, mode)
    DRM_CHECK(find_display_resources(), "Failed to find display resources");
    std::cout << "Found display resources. CRTC ID: " << crtc_->crtc_id
              << ", Connector ID: " << connector_->connector_id
              << ", Mode: " << mode_->name << " (" << mode_->hdisplay << "x" << mode_->vdisplay << ")" << std::endl;

    // 4. Initialize GBM
    gbm_device_ = gbm_create_device(drm_fd_);
    DRM_CHECK(gbm_device_ != nullptr, "Failed to create GBM device");
    std::cout << "GBM device created." << std::endl;

    // 5. Setup GBM buffers and DRM framebuffers (double buffering)
    DRM_CHECK(setup_gbm_buffers(width, height), "Failed to setup GBM buffers");
    std::cout << "GBM buffers and DRM framebuffers created." << std::endl;

    // 6. Set CRTC mode and perform initial page flip
    // Set the CRTC to use the first framebuffer
    DRM_CHECK(drmModeSetCrtc(drm_fd_, crtc_->crtc_id, framebuffers_[0].id,
                             0, 0, &connector_->connector_id, 1, mode_) == 0,
              "Failed to set CRTC mode");
    std::cout << "CRTC mode set." << std::endl;

    // Perform an initial page flip to make sure the display is active
    // This is asynchronous, the handler will be called when it's done.
    // We don't need to wait for it here.
    DRM_CHECK(drmModePageFlip(drm_fd_, crtc_->crtc_id, framebuffers_[0].id,
                              DRM_MODE_PAGE_FLIP_EVENT, this) == 0,
              "Failed initial page flip");
    std::cout << "Initial page flip requested." << std::endl;

    return true;
}

// Finds a suitable CRTC, connector, and mode
bool DrmRenderer::find_display_resources() {
    drmModeRes *resources = drmModeGetResources(drm_fd_);
    DRM_CHECK(resources != nullptr, "Failed to get DRM resources");

    // Iterate through connectors
    for (int i = 0; i < resources->count_connectors; ++i) {
        drmModeConnector *conn = drmModeGetConnector(drm_fd_, resources->connectors[i]);
        if (!conn) {
            std::cerr << "Could not get connector " << resources->connectors[i] << std::endl;
            continue;
        }

        // Check if connector is connected and has modes
        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            connector_ = conn;
            mode_ = &conn->modes[0]; // Use the first mode (usually preferred)
            std::cout << "Found connected connector: " << conn->connector_id << std::endl;

            // Find an encoder for this connector
            drmModeEncoder *encoder = nullptr;
            if (conn->encoder_id) {
                encoder = drmModeGetEncoder(drm_fd_, conn->encoder_id);
            } else {
                // If no encoder, try to find one
                for (int j = 0; j < resources->count_encoders; ++j) {
                    encoder = drmModeGetEncoder(drm_fd_, resources->encoders[j]);
                    if (encoder && encoder->connector_id == conn->connector_id) {
                        break;
                    }
                    drmModeFreeEncoder(encoder);
                    encoder = nullptr;
                }
            }
            DRM_CHECK(encoder != nullptr, "Failed to find encoder for connector");
            std::cout << "Found encoder: " << encoder->encoder_id << std::endl;

            // Find a CRTC that can be used with this encoder
            if (encoder->crtc_id) {
                crtc_ = drmModeGetCrtc(drm_fd_, encoder->crtc_id);
            } else {
                // If no CRTC, try to find one
                for (int j = 0; j < resources->count_crtcs; ++j) {
                    if (encoder->possible_crtcs & (1 << j)) {
                        crtc_ = drmModeGetCrtc(drm_fd_, resources->crtcs[j]);
                        break;
                    }
                }
            }
            DRM_CHECK(crtc_ != nullptr, "Failed to find CRTC for encoder");
            std::cout << "Found CRTC: " << crtc_->crtc_id << std::endl;

            // Free encoder (connector is freed later)
            drmModeFreeEncoder(encoder);
            drmModeFreeResources(resources);
            return true;
        }
        drmModeFreeConnector(conn); // Free if not suitable
    }

    drmModeFreeResources(resources);
    return false; // No suitable display found
}

// Sets up GBM buffers and creates corresponding DRM framebuffers
bool DrmRenderer::setup_gbm_buffers(int width, int height) {
    // We'll use double buffering for smooth display
    const int NUM_BUFFERS = 2;
    framebuffers_.resize(NUM_BUFFERS);

    for (int i = 0; i < NUM_BUFFERS; ++i) {
        // Create a GBM buffer object
        // Format: XRGB8888 (32-bit RGB with alpha, common for display)
        // Usage: Scanout (for display), Write (for rendering to it)
        framebuffers_[i].bo.reset(gbm_bo_create(gbm_device_, width, height,
                                                GBM_FORMAT_XRGB8888,
                                                GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING),
                                  gbm_bo_destroy);
        DRM_CHECK(framebuffers_[i].bo != nullptr, "Failed to create GBM buffer object");

        framebuffers_[i].width = gbm_bo_get_width(framebuffers_[i].bo.get());
        framebuffers_[i].height = gbm_bo_get_height(framebuffers_[i].bo.get());
        framebuffers_[i].pitch = gbm_bo_get_stride(framebuffers_[i].bo.get());
        framebuffers_[i].handle = gbm_bo_get_handle(framebuffers_[i].bo.get()).u32;
        // framebuffers_[i].size = framebuffers_[i].pitch * framebuffers_[i].height; // size is not directly used for mmap below

        // Add DRM framebuffer
        DRM_CHECK(drmModeAddFB(drm_fd_, framebuffers_[i].width, framebuffers_[i].height,
                               24, 32, framebuffers_[i].pitch, framebuffers_[i].handle,
                               &framebuffers_[i].id) == 0,
                  "Failed to add DRM framebuffer");

        // Map the GBM buffer for CPU access (for YUV to RGB conversion and copying)
        // Note: gbm_bo_map returns the mapped pointer and stride directly, no need for separate mmap
        int map_stride;
        framebuffers_[i].map_ptr = gbm_bo_map(framebuffers_[i].bo.get(), 0, 0,
                                              framebuffers_[i].width, framebuffers_[i].height,
                                              GBM_BO_TRANSFER_WRITE, &map_stride, nullptr); // Last arg is for plane_count, not needed here
        DRM_CHECK(framebuffers_[i].map_ptr != MAP_FAILED, "Failed to map GBM buffer");

        std::cout << "Created DRM FB " << framebuffers_[i].id << " with GBM BO "
                  << framebuffers_[i].handle << ", mapped at " << framebuffers_[i].map_ptr << std::endl;
    }

    return true;
}

// Converts YUV420 to XRGB8888 (packed 32-bit RGB with alpha)
// This is a basic CPU conversion. For performance, consider GPU shaders.
void DrmRenderer::yuv420_to_xrgb8888(const void *y_data, const void *u_data, const void *v_data,
                                     int y_stride, int u_stride, int v_stride,
                                     int width, int height, uint8_t *rgb_buffer, int rgb_stride) {
    const uint8_t *Y = static_cast<const uint8_t*>(y_data);
    const uint8_t *U = static_cast<const uint8_t*>(u_data);
    const uint8_t *V = static_cast<const uint8_t*>(v_data);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Get YUV values, clamping to valid ranges
            int Y_val = Y[y * y_stride + x];
            int U_val = U[(y / 2) * u_stride + (x / 2)];
            int V_val = V[(y / 2) * v_stride + (x / 2)];

            // YCbCr to RGB conversion (ITU-R BT.601 standard for full range)
            // Values are typically 0-255 for Y, U, V
            // R = Y + 1.402 * (V - 128)
            // G = Y - 0.344136 * (U - 128) - 0.714136 * (V - 128)
            // B = Y + 1.772 * (U - 128)
            // Using integer arithmetic to avoid floats
            int C = Y_val - 16;
            int U_comp = U_val - 128;
            int V_comp = V_val - 128;

            int R = (298 * C + 409 * V_comp + 128) >> 8;
            int G = (298 * C - 100 * U_comp - 208 * V_comp + 128) >> 8;
            int B = (298 * C + 516 * U_comp + 128) >> 8;

            // Clamp values to [0, 255]
            R = std::max(0, std::min(255, R));
            G = std::max(0, std::min(255, G));
            B = std::max(0, std::min(255, B));

            // Store as XRGB8888 (0xXXRRGGBB)
            uint32_t *pixel = reinterpret_cast<uint32_t*>(rgb_buffer + y * rgb_stride + x * 4);
            *pixel = (0xFF << 24) | (R << 16) | (G << 8) | B;
        }
    }
}


void DrmRenderer::render_frame(const void *y_data, const void *u_data, const void *v_data,
                               int y_stride, int u_stride, int v_stride,
                               int width, int height, const libcamera::PixelFormat &format) {
    if (drm_fd_ == -1 || !mode_ || framebuffers_.empty()) {
        std::cerr << "DRM Renderer not initialized." << std::endl;
        return;
    }

    // Determine the next buffer to render to (double buffering)
    int next_fb_idx = (current_fb_idx_ + 1) % framebuffers_.size();
    DrmFb &next_fb = framebuffers_[next_fb_idx];

    // Ensure the format is YUV420 for our conversion
    if (format != libcamera::formats::YUV420) {
        std::cerr << "Unsupported pixel format for DRM rendering: " << format.toString() << std::endl;
        return;
    }

    // Perform YUV to XRGB8888 conversion directly into the mapped GBM buffer
    yuv420_to_xrgb8888(y_data, u_data, v_data,
                       y_stride, u_stride, v_stride,
                       width, height,
                       static_cast<uint8_t*>(next_fb.map_ptr), next_fb.pitch);

    // Request a page flip to display the new buffer
    // The 'this' pointer is passed as user_data to the page_flip_handler
    int ret = drmModePageFlip(drm_fd_, crtc_->crtc_id, next_fb.id,
                              DRM_MODE_PAGE_FLIP_EVENT, this);
    if (ret != 0) {
        // This can happen if a flip is already pending.
        // In a real application, you'd handle this by skipping the frame
        // or waiting for the previous flip to complete.
        // For simplicity, we just print an error.
        std::cerr << "drmModePageFlip failed: " << strerror(errno) << std::endl;
    } else {
        current_fb_idx_ = next_fb_idx; // Update current buffer only on successful flip request
    }
}

// Static page flip handler
void DrmRenderer::page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec,
                                    unsigned int tv_usec, void *user_data) {
    // This handler is called when a page flip completes.
    // In a real application, you might use this to synchronize rendering,
    // but for this simple example, we just acknowledge it.
    // std::cout << "Page flip completed. Sequence: " << sequence << std::endl;
}


void DrmRenderer::cleanup() {
    // Unmap GBM buffers
    for (auto &fb : framebuffers_) {
        if (fb.map_ptr != MAP_FAILED && fb.map_ptr != nullptr) {
            gbm_bo_unmap(fb.bo.get(), fb.map_ptr);
            fb.map_ptr = nullptr;
        }
        // gbm_bo_destroy is called by unique_ptr's custom deleter
        // drmModeRmFB is implicitly called when gbm_bo is destroyed if it was added via drmModeAddFB2
        // For drmModeAddFB, you might need drmModeRmFB explicitly, but gbm_bo_destroy usually handles it.
        if (fb.id != 0) {
            drmModeRmFB(drm_fd_, fb.id);
            fb.id = 0;
        }
    }
    framebuffers_.clear();

    // Free GBM device
    if (gbm_device_) {
        gbm_device_destroy(gbm_device_);
        gbm_device_ = nullptr;
    }

    // Free DRM resources
    if (crtc_) {
        // Restore original CRTC mode if desired, otherwise just free
        // drmModeSetCrtc(drm_fd_, crtc_->crtc_id, crtc_->buffer_id,
        //                crtc_->x, crtc_->y, &connector_->connector_id, 1, &crtc_->mode);
        drmModeFreeCrtc(crtc_);
        crtc_ = nullptr;
    }
    if (connector_) {
        drmModeFreeConnector(connector_);
        connector_ = nullptr;
    }
    // mode_ is part of connector_, so no separate free needed if connector_ is freed
    mode_ = nullptr;


    // Close DRM file descriptor
    if (drm_fd_ != -1) {
        close(drm_fd_);
        drm_fd_ = -1;
    }
    std::cout << "DRM Renderer cleaned up." << std::endl;
}
