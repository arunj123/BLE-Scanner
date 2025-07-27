#include <libcamera/libcamera.h>
#include <libcamera/camera.h>
#include <libcamera/camera_manager.h>
#include <libcamera/framebuffer.h>
#include <libcamera/stream.h>
#include <libcamera/controls.h> // For optional camera controls
#include <libcamera/formats.h>  // For libcamera::formats::YUV420

#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <map>
#include <chrono> // For timing
#include <atomic> // For atomic flag
#include <poll.h> // For poll() to handle DRM events
#include <cstring> // For strerror

// For memory mapping framebuffers
#include <sys/mmap.h>
#include <fcntl.h>
#include <unistd.h>

// For DRM event context (drmEventContext, drmHandleEvent)
#include <xf86drm.h>

// Include our new DRM Renderer header
#include "drm_renderer.h"

// Global variables for camera and requests, managed by smart pointers
std::shared_ptr<libcamera::Camera> g_camera;
// Changed to store raw pointers, as libcamera's allocator owns the unique_ptrs
std::map<libcamera::Stream *, std::vector<libcamera::FrameBuffer*>> g_frameBuffers;
libcamera::Stream *g_videoStream = nullptr;
std::vector<std::unique_ptr<libcamera::Request>> g_requests;
std::atomic<bool> g_running = true; // Flag to control the main loop

// Global instance of our DRM renderer
std::unique_ptr<DrmRenderer> g_drmRenderer;

// Added: Global unique_ptr for CameraManager as create() returns unique_ptr
std::unique_ptr<libcamera::CameraManager> g_camera_manager;


// Callback function for when a camera request is completed
void request_completed(libcamera::Request *request) {
    if (!g_running) {
        return; // Don't process if we're shutting down
    }

    // Check if the request was successful
    if (request->status() == libcamera::Request::RequestCancelled) {
        // std::cout << "Request cancelled." << std::endl; // Too noisy
        return;
    } else if (request->status() != libcamera::Request::RequestComplete) {
        std::cerr << "Request failed with status: " << request->status() << std::endl;
        // Re-queue the request even if it failed, to keep the stream going if possible
        g_camera->queueRequest(request);
        return;
    }

    // Iterate through the buffers associated with this request
    for (auto const& bufferPair : request->buffers()) {
        const libcamera::Stream *stream = bufferPair.first; // Fixed: const-correctness
        libcamera::FrameBuffer *buffer = bufferPair.second;

        // Ensure this is our video stream
        if (stream == g_videoStream) {
            const libcamera::StreamConfiguration &streamConfig = stream->configuration();
            int width = streamConfig.size.width;
            int height = streamConfig.size.height;
            libcamera::PixelFormat format = streamConfig.pixelFormat;

            // Ensure the format is YUV420 for our current DRM renderer
            if (format != libcamera::formats::YUV420) {
                std::cerr << "Warning: Camera output format is " << format.toString()
                          << ", but DRM renderer expects YUV420. Skipping frame." << std::endl;
                // Re-queue the request even if we skip processing this frame
                g_camera->queueRequest(request);
                return;
            }

            // Get pointers to the Y, U, V planes
            const void *y_data = nullptr;
            const void *u_data = nullptr;
            const void *v_data = nullptr;
            int y_stride = 0;
            int u_stride = 0;
            int v_stride = 0;

            // Assuming YUV420 with 3 planes (Y, U, V)
            if (buffer->planes().size() >= 3) {
                // Plane 0: Y (Luminance)
                const libcamera::FrameBuffer::Plane &y_plane = buffer->planes()[0];
                y_data = mmap(NULL, y_plane.length, PROT_READ, MAP_SHARED, y_plane.fd.get(), y_plane.offset);
                y_stride = streamConfig.stride; // Get stride from StreamConfiguration
                if (y_data == MAP_FAILED) {
                    std::cerr << "Failed to mmap Y plane: " << strerror(errno) << std::endl;
                    // Re-queue and return if mmap fails
                    g_camera->queueRequest(request);
                    return;
                }

                // Plane 1: U (Chroma Blue)
                const libcamera::FrameBuffer::Plane &u_plane = buffer->planes()[1];
                u_data = mmap(NULL, u_plane.length, PROT_READ, MAP_SHARED, u_plane.fd.get(), u_plane.offset);
                u_stride = streamConfig.stride / 2; // Derived stride for U plane (YUV420)
                if (u_data == MAP_FAILED) {
                    std::cerr << "Failed to mmap U plane: " << strerror(errno) << std::endl;
                    munmap((void*)y_data, y_plane.length); // Clean up Y plane
                    g_camera->queueRequest(request);
                    return;
                }

                // Plane 2: V (Chroma Red)
                const libcamera::FrameBuffer::Plane &v_plane = buffer->planes()[2];
                v_data = mmap(NULL, v_plane.length, PROT_READ, MAP_SHARED, v_plane.fd.get(), v_plane.offset);
                v_stride = streamConfig.stride / 2; // Derived stride for V plane (YUV420)
                if (v_data == MAP_FAILED) {
                    std::cerr << "Failed to mmap V plane: " << strerror(errno) << std::endl;
                    munmap((void*)y_data, y_plane.length); // Clean up Y plane
                    munmap((void*)u_data, u_plane.length); // Clean up U plane
                    g_camera->queueRequest(request);
                    return;
                }

                // Call the DRM renderer to display the frame
                if (g_drmRenderer) {
                    g_drmRenderer->render_frame(y_data, u_data, v_data,
                                                y_stride, u_stride, v_stride,
                                                width, height, format);
                }

                // Unmap all planes after use
                munmap((void*)y_data, y_plane.length);
                munmap((void*)u_data, u_plane.length);
                munmap((void*)v_data, v_plane.length);

            } else {
                std::cerr << "Warning: Expected 3 planes for YUV420, but got " << buffer->planes().size() << std::endl;
            }
        }
    }

    // Re-queue the request to capture the next frame
    g_camera->queueRequest(request);
}

int main() {
    std::cout << "Starting libcamera USB camera example..." << std::endl;

    // 1. Initialize libcamera CameraManager
    // Reverted to CameraManager::create() as libcamera0.5 is now installed
    g_camera_manager = libcamera::CameraManager::create();
    
    int ret = g_camera_manager->start(); // Use the unique_ptr
    if (ret) {
        std::cerr << "Failed to start camera manager: " << ret << std::endl;
        return -1;
    }

    if (g_camera_manager->cameras().empty()) { // Use the unique_ptr
        std::cerr << "No cameras found! Make sure your USB camera is connected." << std::endl;
        g_camera_manager->stop(); // Use the unique_ptr
        return -1;
    }

    // 2. Get the first camera
    g_camera = g_camera_manager->cameras()[0]; // Use the unique_ptr
    std::cout << "Found camera: " << g_camera->id() << std::endl;

    // 3. Acquire the camera
    ret = g_camera->acquire();
    if (ret) {
        std::cerr << "Failed to acquire camera: " << ret << std::endl;
        g_camera_manager->stop(); // Use the unique_ptr
        return -1;
    }

    // 4. Generate and configure stream
    // Request a Viewfinder role for live preview, which is efficient.
    std::unique_ptr<libcamera::CameraConfiguration> config = g_camera->generateConfiguration({libcamera::StreamRole::Viewfinder});
    if (!config) {
        std::cerr << "Failed to generate camera configuration!" << std::endl;
        g_camera->release();
        g_camera_manager->stop(); // Use the unique_ptr
        return -1;
    }

    // Set desired resolution and pixel format
    // For this example, we explicitly request YUV420 as our DRM renderer expects it.
    config->at(0).pixelFormat = libcamera::formats::YUV420;
    config->at(0).size.width = 640;
    config->at(0).size.height = 480;
    config->at(0).bufferCount = 4; // Use a few buffers for smooth streaming

    // Validate and apply the configuration
    ret = config->validate();
    if (ret == libcamera::CameraConfiguration::Invalid) {
        std::cerr << "Invalid camera configuration! Your camera might not support YUV420 640x480." << std::endl;
        g_camera->release();
        g_camera_manager->stop(); // Use the unique_ptr
        return -1;
    } else if (ret == libcamera::CameraConfiguration::Adjusted) {
        std::cout << "Camera configuration adjusted to: " << config->at(0).toString() << std::endl;
    } else {
        std::cout << "Camera configured with: " << config->at(0).toString() << std::endl;
    }

    ret = g_camera->configure(config.get());
    if (ret) {
        std::cerr << "Failed to configure camera: " << ret << std::endl;
        g_camera->release();
        g_camera_manager->stop(); // Use the unique_ptr
        return -1;
    }

    g_videoStream = config->at(0).stream(); // Fixed: Call stream() method

    // 5. Initialize the DRM renderer with the desired display resolution
    g_drmRenderer = std::make_unique<DrmRenderer>();
    if (!g_drmRenderer->init(config->at(0).size.width, config->at(0).size.height)) {
        std::cerr << "Failed to initialize DRM renderer. Exiting." << std::endl;
        g_camera->release();
        g_camera_manager->stop(); // Use the unique_ptr
        return -1;
    }

    // 6. Allocate frame buffers for the stream
    libcamera::FrameBufferAllocator allocator(g_camera);
    ret = allocator.allocate(g_videoStream);
    if (ret < 0) {
        std::cerr << "Failed to allocate buffers: " << ret << std::endl;
        g_camera->release();
        g_drmRenderer->cleanup();
        g_camera_manager->stop(); // Use the unique_ptr
        return -1;
    }
    // Fixed: Iterate over the unique_ptrs from allocator and store raw pointers
    const auto &allocated_buffers = allocator.buffers(g_videoStream);
    for (const auto &buffer_uptr : allocated_buffers) {
        g_frameBuffers[g_videoStream].push_back(buffer_uptr.get()); // Store raw pointer
    }


    // 7. Create and queue requests for each buffer (using the raw pointers)
    for (auto *buffer : g_frameBuffers[g_videoStream]) { // Fixed: Iterate over raw pointers now
        std::unique_ptr<libcamera::Request> request = g_camera->createRequest();
        if (!request) {
            std::cerr << "Failed to create request!" << std::endl;
            g_camera->release();
            g_drmRenderer->cleanup();
            g_camera_manager->stop(); // Use the unique_ptr
            return -1;
        }
        ret = request->addBuffer(g_videoStream, buffer); // Fixed: Use raw pointer
        if (ret) {
            std::cerr << "Failed to add buffer to request: " << ret << std::endl;
            g_camera->release();
            g_drmRenderer->cleanup();
            g_camera_manager->stop(); // Use the unique_ptr
            return -1;
        }
        g_requests.push_back(std::move(request));
        g_camera->queueRequest(g_requests.back().get());
    }

    // 8. Connect the request completion signal to our callback
    g_camera->requestCompleted.connect(request_completed);

    // 9. Start the camera
    ret = g_camera->start();
    if (ret) {
        std::cerr << "Failed to start camera: " << ret << std::endl;
        g_camera->release();
        g_drmRenderer->cleanup();
        g_camera_manager->stop(); // Use the unique_ptr
        return -1;
    }

    std::cout << "\nCamera started. Capturing frames and rendering to DRM. Press Enter to stop.\n" << std::endl;

    // Main loop to handle DRM events (page flips)
    // Fixed: Use getDrmFd()
    struct pollfd pfd = { .fd = g_drmRenderer->getDrmFd(), .events = POLLIN };
    while (g_running) {
        // Poll for DRM events (e.g., page flip completion)
        // Timeout of 100ms to allow checking g_running flag
        if (poll(&pfd, 1, 100) > 0) {
            // Fixed: drmEventContext and drmHandleEvent are in <xf86drm.h>
            drmEventContext evctx = {
                .version = DRM_EVENT_CONTEXT_VERSION,
                .page_flip_handler = DrmRenderer::page_flip_handler
            };
            // Fixed: Use getDrmFd() and evctx
            drmHandleEvent(g_drmRenderer->getDrmFd(), &evctx);
        }
    }

    std::cout << "Stopping camera..." << std::endl;

    // Cleanup
    g_camera->stop();
    allocator.free(g_videoStream);
    g_camera->release();
    g_camera_manager->stop(); // Use the unique_ptr
    g_drmRenderer->cleanup(); // Ensure DRM resources are freed

    std::cout << "Program finished." << std::endl;

    return 0;
}
