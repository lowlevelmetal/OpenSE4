// Vulkan 1.3 backend.
//
// - The Vulkan loader is loaded at runtime through SDL and volk, so the game
//   still starts on machines without Vulkan (falling back to OpenGL).
// - Dynamic rendering + synchronization2, no render passes or framebuffers.
// - Two frames in flight. Resources that recorded commands might still use are
//   destroyed only after that frame slot's fence has been waited on.
// - Per-frame streaming buffer for the transient 2D geometry.

#include "gfx/device.hpp"

#include "core/log.hpp"
#include "shaders/basic2d_frag.h"
#include "shaders/basic2d_vert.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <stdexcept>
#include <utility>

namespace opense4::gfx {

namespace {

constexpr uint32_t kFramesInFlight = 2;
constexpr uint32_t kMaxTextures = 1024;
constexpr VkDeviceSize kInitialStreamSize = 4ull << 20;

const char* resultName(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        default: return "VkResult(unknown)";
    }
}

struct VulkanError : std::runtime_error {
    explicit VulkanError(const std::string& what) : std::runtime_error(what) {}
};

void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw VulkanError(std::format("{} failed: {} ({})", what, resultName(r), static_cast<int>(r)));
}
#define VK_CHECK(expr) check((expr), #expr)

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) log::error("[vulkan] {}", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) log::warn("[vulkan] {}", data->pMessage);
    else log::debug("[vulkan] {}", data->pMessage);
    return VK_FALSE;
}

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;
};

struct Frame {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    Buffer stream;
    VkDeviceSize streamUsed = 0;
    std::vector<Buffer> retiredBuffers;  // freed when this slot comes around again
    std::vector<Texture> retiredTextures;
};

class VulkanDevice final : public Device {
public:
    VulkanDevice(SDL_Window* window, const DeviceOptions& options) : window_(window), vsync_(options.vsync) {}
    ~VulkanDevice() override { shutdown(); }

    void init(bool validation) {
        createInstance(validation);
        if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_))
            throw VulkanError(std::format("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError()));
        pickPhysicalDevice();
        createLogicalDevice();
        createAllocator();
        createDescriptorsAndSamplers();
        createFrames();
        chooseSurfaceFormat();
        createSwapchain();
        createPipeline();

        const uint32_t white = 0xffffffffu;
        whiteTexture_ = createTexture(TextureDesc{1, 1, Filter::Nearest, "white"}, &white);
    }

    Backend backend() const override { return Backend::Vulkan; }
    const std::string& deviceName() const override { return deviceName_; }

    TextureId createTexture(const TextureDesc& desc, const void* rgba) override {
        Texture tex;
        tex.width = desc.width;
        tex.height = desc.height;

        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.extent = {static_cast<uint32_t>(desc.width), static_cast<uint32_t>(desc.height), 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK(vmaCreateImage(allocator_, &ici, &aci, &tex.image, &tex.allocation, nullptr));

        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = tex.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = ici.format;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device_, &vci, nullptr, &tex.view));

        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorPool = descriptorPool_;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &setLayout_;
        VK_CHECK(vkAllocateDescriptorSets(device_, &dai, &tex.descriptor));
        VkDescriptorImageInfo imageInfo{desc.filter == Filter::Linear ? linearSampler_ : nearestSampler_, tex.view,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = tex.descriptor;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

        std::vector<uint8_t> zeros;
        if (!rgba) {
            zeros.assign(static_cast<size_t>(desc.width) * static_cast<size_t>(desc.height) * 4, 0);
            rgba = zeros.data();
        }
        upload(tex, 0, 0, desc.width, desc.height, rgba, desc.width * 4, /*initial=*/true);

        uint32_t slot;
        if (!freeTextureSlots_.empty()) {
            slot = freeTextureSlots_.back();
            freeTextureSlots_.pop_back();
            textures_[slot] = tex;
        } else {
            slot = static_cast<uint32_t>(textures_.size());
            textures_.push_back(tex);
        }
        return TextureId{slot + 1};
    }

    void updateTexture(TextureId id, int x, int y, int width, int height, const void* rgba, int pitchBytes) override {
        if (Texture* tex = lookup(id)) upload(*tex, x, y, width, height, rgba, pitchBytes, /*initial=*/false);
    }

    void destroyTexture(TextureId id) override {
        Texture* tex = lookup(id);
        if (!tex || id == whiteTexture_) return;
        // Inside a frame the current slot's fence covers every use. Between frames the
        // latest submission belongs to the previous slot (endFrame already advanced).
        const uint32_t slot = inFrame_ ? frameIndex_ : (frameIndex_ + kFramesInFlight - 1) % kFramesInFlight;
        frames_[slot].retiredTextures.push_back(*tex);
        *tex = Texture{};
        freeTextureSlots_.push_back(id.value - 1);
    }

    std::optional<FrameInfo> beginFrame(Color clear) override {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if (w <= 0 || h <= 0 || (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED)) return std::nullopt;
        if (swapchainDirty_ || swapchain_ == VK_NULL_HANDLE || w != requestedWidth_ || h != requestedHeight_) {
            createSwapchain();
            if (swapchain_ == VK_NULL_HANDLE) return std::nullopt;
        }

        Frame& f = frames_[frameIndex_];
        VK_CHECK(vkWaitForFences(device_, 1, &f.fence, VK_TRUE, UINT64_MAX));
        releaseRetired(f);

        const VkResult acquire = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, f.imageAvailable, VK_NULL_HANDLE, &imageIndex_);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchainDirty_ = true;
            return std::nullopt;
        }
        if (acquire == VK_SUBOPTIMAL_KHR) swapchainDirty_ = true;
        else check(acquire, "vkAcquireNextImageKHR");

        VK_CHECK(vkResetFences(device_, 1, &f.fence));
        VK_CHECK(vkResetCommandPool(device_, f.pool, 0));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(f.cmd, &bi));
        f.streamUsed = 0;

        imageBarrier(f.cmd, swapchainImages_[imageIndex_], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = swapchainViews_[imageIndex_];
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{clear.r, clear.g, clear.b, clear.a}};
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, extent_};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &color;
        vkCmdBeginRendering(f.cmd, &ri);

        vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        const VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent_.width), static_cast<float>(extent_.height), 0.0f, 1.0f};
        vkCmdSetViewport(f.cmd, 0, 1, &viewport);
        boundTexture_ = VK_NULL_HANDLE;
        inFrame_ = true;
        return FrameInfo{extent_.width, extent_.height};
    }

    void draw(const DrawBatch& batch) override {
        if (!inFrame_ || batch.indices.empty() || batch.vertices.empty()) return;
        Frame& f = frames_[frameIndex_];

        const VkDeviceSize vertexBytes = batch.vertices.size_bytes();
        const VkDeviceSize indexBytes = batch.indices.size_bytes();
        const VkDeviceSize vertexOffset = align(f.streamUsed, 16);
        const VkDeviceSize indexOffset = align(vertexOffset + vertexBytes, 16);
        VkDeviceSize end = indexOffset + indexBytes;
        if (end > f.stream.size) {
            // Keep the old buffer alive until this frame completes; later draws use a bigger one.
            f.retiredBuffers.push_back(f.stream);
            f.stream = createStreamBuffer(std::max(f.stream.size * 2, align(vertexBytes, 16) + indexBytes + kInitialStreamSize));
            f.streamUsed = 0;
            draw(batch);
            return;
        }
        auto* base = static_cast<uint8_t*>(f.stream.mapped);
        std::memcpy(base + vertexOffset, batch.vertices.data(), vertexBytes);
        std::memcpy(base + indexOffset, batch.indices.data(), indexBytes);
        f.streamUsed = end;

        vkCmdBindVertexBuffers(f.cmd, 0, 1, &f.stream.buffer, &vertexOffset);
        vkCmdBindIndexBuffer(f.cmd, f.stream.buffer, indexOffset, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(f.cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), batch.transform.m.data());

        for (const DrawCommand& dc : batch.commands) {
            if (dc.indexCount == 0) continue;
            const int32_t x0 = std::clamp(dc.scissor.x, 0, static_cast<int32_t>(extent_.width));
            const int32_t y0 = std::clamp(dc.scissor.y, 0, static_cast<int32_t>(extent_.height));
            const int32_t x1 = std::clamp(dc.scissor.x + static_cast<int32_t>(dc.scissor.width), 0, static_cast<int32_t>(extent_.width));
            const int32_t y1 = std::clamp(dc.scissor.y + static_cast<int32_t>(dc.scissor.height), 0, static_cast<int32_t>(extent_.height));
            if (x1 <= x0 || y1 <= y0) continue;
            const VkRect2D rect{{x0, y0}, {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)}};
            vkCmdSetScissor(f.cmd, 0, 1, &rect);

            const Texture* tex = lookup(dc.texture);
            if (!tex) tex = lookup(whiteTexture_);
            if (tex->descriptor != boundTexture_) {
                vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &tex->descriptor, 0, nullptr);
                boundTexture_ = tex->descriptor;
            }
            vkCmdDrawIndexed(f.cmd, dc.indexCount, 1, dc.firstIndex, dc.vertexOffset, 0);
        }
    }

    void endFrame() override {
        if (!inFrame_) return;
        inFrame_ = false;
        Frame& f = frames_[frameIndex_];
        vkCmdEndRendering(f.cmd);

        const VkImage image = swapchainImages_[imageIndex_];
        const bool capture = captureRequested_ && swapchainSupportsCopy_ && captureFormatOk_;
        if (capture) {
            captureRequested_ = false;
            prepareCaptureBuffer();
            imageBarrier(f.cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {extent_.width, extent_.height, 1};
            vkCmdCopyImageToBuffer(f.cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, capture_.buffer, 1, &region);
            VkBufferMemoryBarrier2 toHost{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            toHost.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            toHost.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            toHost.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            toHost.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            toHost.srcQueueFamilyIndex = toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toHost.buffer = capture_.buffer;
            toHost.size = VK_WHOLE_SIZE;
            VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dep.bufferMemoryBarrierCount = 1;
            dep.pBufferMemoryBarriers = &toHost;
            vkCmdPipelineBarrier2(f.cmd, &dep);
            imageBarrier(f.cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                         VK_PIPELINE_STAGE_2_COPY_BIT, 0, VK_PIPELINE_STAGE_2_NONE, 0);
        } else {
            imageBarrier(f.cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                         VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_NONE, 0);
        }
        VK_CHECK(vkEndCommandBuffer(f.cmd));

        VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        wait.semaphore = f.imageAvailable;
        wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signal.semaphore = renderFinished_[imageIndex_];
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmdInfo.commandBuffer = f.cmd;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &wait;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmdInfo;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signal;
        VK_CHECK(vkQueueSubmit2(queue_, 1, &submit, f.fence));

        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &renderFinished_[imageIndex_];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &imageIndex_;
        const VkResult pr = vkQueuePresentKHR(queue_, &present);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) swapchainDirty_ = true;
        else check(pr, "vkQueuePresentKHR");

        if (capture) {
            VK_CHECK(vkWaitForFences(device_, 1, &f.fence, VK_TRUE, UINT64_MAX));
            readCapture();
        }
        frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
    }

    void setVSync(bool enabled) override {
        if (enabled == vsync_) return;
        vsync_ = enabled;
        swapchainDirty_ = true;
    }

    void requestCapture() override { captureRequested_ = true; }
    std::optional<Image> takeCapture() override { return std::exchange(captured_, std::nullopt); }

    void waitIdle() override {
        if (device_) vkDeviceWaitIdle(device_);
    }

private:
    static VkDeviceSize align(VkDeviceSize v, VkDeviceSize a) { return (v + a - 1) & ~(a - 1); }

    Texture* lookup(TextureId id) {
        if (!id || id.value > textures_.size()) return nullptr;
        Texture& t = textures_[id.value - 1];
        return t.image ? &t : nullptr;
    }

    // --- Initialization --------------------------------------------------------------
    void createInstance(bool validation) {
        auto getProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
        if (!getProc) throw VulkanError(std::format("Vulkan loader not available: {}", SDL_GetError()));
        volkInitializeCustom(getProc);

        uint32_t loaderVersion = VK_API_VERSION_1_0;
        if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&loaderVersion);
        if (loaderVersion < VK_API_VERSION_1_3)
            throw VulkanError(std::format("Vulkan loader is version {}.{}, 1.3 required", VK_API_VERSION_MAJOR(loaderVersion),
                                          VK_API_VERSION_MINOR(loaderVersion)));

        Uint32 sdlExtCount = 0;
        const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
        if (!sdlExts) throw VulkanError(std::format("SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError()));
        std::vector<const char*> extensions(sdlExts, sdlExts + sdlExtCount);
        std::vector<const char*> layers;

        if (validation) {
            uint32_t count = 0;
            vkEnumerateInstanceLayerProperties(&count, nullptr);
            std::vector<VkLayerProperties> props(count);
            vkEnumerateInstanceLayerProperties(&count, props.data());
            const bool haveLayer = std::any_of(props.begin(), props.end(), [](const VkLayerProperties& p) {
                return std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0;
            });
            if (haveLayer) {
                layers.push_back("VK_LAYER_KHRONOS_validation");
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                debugUtils_ = true;
            } else {
                log::warn("Vulkan validation requested but VK_LAYER_KHRONOS_validation is not installed");
            }
        }

        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "opense4";
        app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        app.pEngineName = "opense4";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        ci.ppEnabledExtensionNames = extensions.data();
        ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
        ci.ppEnabledLayerNames = layers.data();
        VK_CHECK(vkCreateInstance(&ci, nullptr, &instance_));
        volkLoadInstanceOnly(instance_);

        if (debugUtils_) {
            VkDebugUtilsMessengerCreateInfoEXT dci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            dci.pfnUserCallback = debugCallback;
            VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance_, &dci, nullptr, &messenger_));
        }
    }

    void pickPhysicalDevice() {
        uint32_t count = 0;
        VK_CHECK(vkEnumeratePhysicalDevices(instance_, &count, nullptr));
        std::vector<VkPhysicalDevice> devices(count);
        VK_CHECK(vkEnumeratePhysicalDevices(instance_, &count, devices.data()));

        int bestScore = -1;
        std::string rejected;
        for (VkPhysicalDevice pd : devices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(pd, &props);
            auto reject = [&](std::string_view why) { rejected += std::format("\n  {}: {}", props.deviceName, why); };

            if (props.apiVersion < VK_API_VERSION_1_3) {
                reject("Vulkan 1.3 not supported");
                continue;
            }
            VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            f2.pNext = &f13;
            vkGetPhysicalDeviceFeatures2(pd, &f2);
            if (!f13.dynamicRendering || !f13.synchronization2) {
                reject("missing dynamicRendering/synchronization2");
                continue;
            }

            uint32_t extCount = 0;
            vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> exts(extCount);
            vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, exts.data());
            if (std::none_of(exts.begin(), exts.end(),
                             [](const auto& e) { return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0; })) {
                reject("no swapchain support");
                continue;
            }

            uint32_t qfCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, nullptr);
            std::vector<VkQueueFamilyProperties> qfs(qfCount);
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, qfs.data());
            std::optional<uint32_t> family;
            for (uint32_t i = 0; i < qfCount && !family; ++i) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &present);
                if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) family = i;
            }
            if (!family) {
                reject("no queue family can both render and present");
                continue;
            }

            int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                        : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                        : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU            ? 0
                                                                                     : 1;
            if (score > bestScore) {
                bestScore = score;
                physicalDevice_ = pd;
                queueFamily_ = *family;
                deviceName_ = std::format("{} (Vulkan {}.{}.{})", props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
                                          VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion));
            }
        }
        if (!physicalDevice_) throw VulkanError("No suitable Vulkan GPU found." + rejected);
    }

    void createLogicalDevice() {
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = queueFamily_;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;

        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        f13.dynamicRendering = VK_TRUE;
        f13.synchronization2 = VK_TRUE;
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        f2.pNext = &f13;

        const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        ci.pNext = &f2;
        ci.queueCreateInfoCount = 1;
        ci.pQueueCreateInfos = &qci;
        ci.enabledExtensionCount = 1;
        ci.ppEnabledExtensionNames = extensions;
        VK_CHECK(vkCreateDevice(physicalDevice_, &ci, nullptr, &device_));
        volkLoadDevice(device_);
        vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = queueFamily_;
        VK_CHECK(vkCreateCommandPool(device_, &pci, nullptr, &uploadPool_));
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VK_CHECK(vkCreateFence(device_, &fci, nullptr, &uploadFence_));
    }

    void createAllocator() {
        VmaVulkanFunctions fns{};
        fns.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        fns.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        VmaAllocatorCreateInfo ci{};
        ci.vulkanApiVersion = VK_API_VERSION_1_3;
        ci.physicalDevice = physicalDevice_;
        ci.device = device_;
        ci.instance = instance_;
        ci.pVulkanFunctions = &fns;
        VK_CHECK(vmaCreateAllocator(&ci, &allocator_));
    }

    void createDescriptorsAndSamplers() {
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.maxLod = 0.0f;
        VK_CHECK(vkCreateSampler(device_, &sci, nullptr, &linearSampler_));
        sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
        VK_CHECK(vkCreateSampler(device_, &sci, nullptr, &nearestSampler_));

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        lci.bindingCount = 1;
        lci.pBindings = &binding;
        VK_CHECK(vkCreateDescriptorSetLayout(device_, &lci, nullptr, &setLayout_));

        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures};
        VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pci.maxSets = kMaxTextures;
        pci.poolSizeCount = 1;
        pci.pPoolSizes = &size;
        VK_CHECK(vkCreateDescriptorPool(device_, &pci, nullptr, &descriptorPool_));
    }

    void createFrames() {
        for (Frame& f : frames_) {
            VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            pci.queueFamilyIndex = queueFamily_;
            VK_CHECK(vkCreateCommandPool(device_, &pci, nullptr, &f.pool));
            VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            ai.commandPool = f.pool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            VK_CHECK(vkAllocateCommandBuffers(device_, &ai, &f.cmd));
            VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            VK_CHECK(vkCreateFence(device_, &fci, nullptr, &f.fence));
            VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VK_CHECK(vkCreateSemaphore(device_, &sci, nullptr, &f.imageAvailable));
            f.stream = createStreamBuffer(kInitialStreamSize);
        }
    }

    Buffer createStreamBuffer(VkDeviceSize size) {
        Buffer b;
        b.size = size;
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = size;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        aci.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        VmaAllocationInfo info{};
        VK_CHECK(vmaCreateBuffer(allocator_, &bci, &aci, &b.buffer, &b.allocation, &info));
        b.mapped = info.pMappedData;
        return b;
    }

    void destroyBuffer(Buffer& b) {
        if (b.buffer) vmaDestroyBuffer(allocator_, b.buffer, b.allocation);
        b = Buffer{};
    }

    void destroyTextureNow(Texture& t) {
        if (t.descriptor) vkFreeDescriptorSets(device_, descriptorPool_, 1, &t.descriptor);
        if (t.view) vkDestroyImageView(device_, t.view, nullptr);
        if (t.image) vmaDestroyImage(allocator_, t.image, t.allocation);
        t = Texture{};
    }

    void releaseRetired(Frame& f) {
        for (Buffer& b : f.retiredBuffers) destroyBuffer(b);
        f.retiredBuffers.clear();
        for (Texture& t : f.retiredTextures) destroyTextureNow(t);
        f.retiredTextures.clear();
    }

    // Chosen once: the pipeline is baked for this format.
    void chooseSurfaceFormat() {
        uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &count, formats.data());
        if (formats.empty()) throw VulkanError("Surface reports no formats");
        // UNORM (not sRGB) to match OpenGL's default framebuffer, so both backends look the same.
        surfaceFormat_ = formats.front();
        for (const auto& f : formats)
            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                surfaceFormat_ = f;
                break;
            }
        const VkFormat fmt = surfaceFormat_.format;
        captureFormatOk_ = fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_R8G8B8A8_UNORM ||
                           fmt == VK_FORMAT_B8G8R8A8_SRGB || fmt == VK_FORMAT_R8G8B8A8_SRGB;
    }

    void createSwapchain() {
        VkSurfaceCapabilitiesKHR caps;
        VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps));

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        VkExtent2D extent = caps.currentExtent;
        if (extent.width == UINT32_MAX) {
            extent.width = std::clamp(static_cast<uint32_t>(std::max(w, 0)), caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(static_cast<uint32_t>(std::max(h, 0)), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        if (extent.width == 0 || extent.height == 0) return;  // minimized; retry next frame

        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount, modes.data());
        auto hasMode = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        if (!vsync_) {
            if (hasMode(VK_PRESENT_MODE_MAILBOX_KHR)) mode = VK_PRESENT_MODE_MAILBOX_KHR;
            else if (hasMode(VK_PRESENT_MODE_IMMEDIATE_KHR)) mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }

        uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(caps.supportedCompositeAlpha & alpha)) {
            for (VkCompositeAlphaFlagBitsKHR a : {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                                  VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
                if (caps.supportedCompositeAlpha & a) {
                    alpha = a;
                    break;
                }
        }
        swapchainSupportsCopy_ = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;

        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        ci.surface = surface_;
        ci.minImageCount = imageCount;
        ci.imageFormat = surfaceFormat_.format;
        ci.imageColorSpace = surfaceFormat_.colorSpace;
        ci.imageExtent = extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (swapchainSupportsCopy_ ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = alpha;
        ci.presentMode = mode;
        ci.clipped = VK_TRUE;
        ci.oldSwapchain = swapchain_;

        // Nothing may still be using the old images when we destroy them.
        if (swapchain_) vkDeviceWaitIdle(device_);
        VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSwapchainKHR(device_, &ci, nullptr, &newSwapchain));
        destroySwapchainResources();
        if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = newSwapchain;
        extent_ = extent;
        requestedWidth_ = w;
        requestedHeight_ = h;
        swapchainDirty_ = false;

        uint32_t count = 0;
        VK_CHECK(vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr));
        swapchainImages_.resize(count);
        VK_CHECK(vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapchainImages_.data()));
        for (VkImage image : swapchainImages_) {
            VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vci.image = image;
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = surfaceFormat_.format;
            vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageView view;
            VK_CHECK(vkCreateImageView(device_, &vci, nullptr, &view));
            swapchainViews_.push_back(view);
            // One "render finished" semaphore per image: presentation may still hold the
            // previous one for this image, so they can't be per-frame-in-flight.
            VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VkSemaphore sem;
            VK_CHECK(vkCreateSemaphore(device_, &sci, nullptr, &sem));
            renderFinished_.push_back(sem);
        }
        log::debug("Vulkan swapchain {}x{}, {} images, present mode {}", extent.width, extent.height, count, static_cast<int>(mode));
    }

    void destroySwapchainResources() {
        for (VkImageView v : swapchainViews_) vkDestroyImageView(device_, v, nullptr);
        for (VkSemaphore s : renderFinished_) vkDestroySemaphore(device_, s, nullptr);
        swapchainViews_.clear();
        renderFinished_.clear();
        swapchainImages_.clear();
    }

    VkShaderModule createShader(const unsigned char* code, size_t size) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = size;
        ci.pCode = reinterpret_cast<const uint32_t*>(code);
        VkShaderModule module;
        VK_CHECK(vkCreateShaderModule(device_, &ci, nullptr, &module));
        return module;
    }

    void createPipeline() {
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4)};
        VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        lci.setLayoutCount = 1;
        lci.pSetLayouts = &setLayout_;
        lci.pushConstantRangeCount = 1;
        lci.pPushConstantRanges = &push;
        VK_CHECK(vkCreatePipelineLayout(device_, &lci, nullptr, &pipelineLayout_));

        const VkShaderModule vert = createShader(shaders::basic2d_vert_spirv, shaders::basic2d_vert_spirv_size);
        const VkShaderModule frag = createShader(shaders::basic2d_frag_spirv, shaders::basic2d_frag_spirv_size);
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr};

        const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const VkVertexInputAttributeDescription attrs[] = {
            {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, x)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, u)},
            {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Vertex, color)},
            {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, mode)},
        };
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = 4;
        vi.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1;
        vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1;
        cb.pAttachments = &blend;
        const VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        ds.dynamicStateCount = 2;
        ds.pDynamicStates = dynamics;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &surfaceFormat_.format;

        VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pci.pNext = &rendering;
        pci.stageCount = 2;
        pci.pStages = stages;
        pci.pVertexInputState = &vi;
        pci.pInputAssemblyState = &ia;
        pci.pViewportState = &vp;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState = &ms;
        pci.pColorBlendState = &cb;
        pci.pDynamicState = &ds;
        pci.layout = pipelineLayout_;
        const VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline_);
        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        check(r, "vkCreateGraphicsPipelines");
    }

    // --- Transfers ----------------------------------------------------------------------
    static void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                             VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                             VkAccessFlags2 dstAccess) {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = srcStage;
        b.srcAccessMask = srcAccess;
        b.dstStageMask = dstStage;
        b.dstAccessMask = dstAccess;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    // Synchronous upload through a staging buffer. Texture uploads are rare (load
    // time, font atlas growth), so simplicity beats throughput here. Queue
    // submission order plus the barrier make this safe even while earlier
    // frames that sample the texture are still in flight.
    void upload(Texture& tex, int x, int y, int width, int height, const void* rgba, int pitchBytes, bool initial) {
        if (width <= 0 || height <= 0) return;
        const size_t rowBytes = static_cast<size_t>(width) * 4;
        Buffer staging;
        staging.size = rowBytes * static_cast<size_t>(height);
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = staging.size;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        VK_CHECK(vmaCreateBuffer(allocator_, &bci, &aci, &staging.buffer, &staging.allocation, &info));
        auto* dst = static_cast<uint8_t*>(info.pMappedData);
        const auto* src = static_cast<const uint8_t*>(rgba);
        for (int row = 0; row < height; ++row)
            std::memcpy(dst + rowBytes * static_cast<size_t>(row), src + static_cast<size_t>(pitchBytes) * static_cast<size_t>(row), rowBytes);
        VK_CHECK(vmaFlushAllocation(allocator_, staging.allocation, 0, VK_WHOLE_SIZE));

        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = uploadPool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd;
        VK_CHECK(vkAllocateCommandBuffers(device_, &ai, &cmd));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

        imageBarrier(cmd, tex.image, initial ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     initial ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                     VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {x, y, 0};
        region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        vkCmdCopyBufferToImage(cmd, staging.buffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        imageBarrier(cmd, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                     VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        VK_CHECK(vkEndCommandBuffer(cmd));

        VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmdInfo.commandBuffer = cmd;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmdInfo;
        VK_CHECK(vkQueueSubmit2(queue_, 1, &submit, uploadFence_));
        VK_CHECK(vkWaitForFences(device_, 1, &uploadFence_, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(device_, 1, &uploadFence_));
        vkFreeCommandBuffers(device_, uploadPool_, 1, &cmd);
        destroyBuffer(staging);
    }

    void prepareCaptureBuffer() {
        const VkDeviceSize size = VkDeviceSize{extent_.width} * extent_.height * 4;
        if (capture_.size == size) return;
        destroyBuffer(capture_);
        capture_.size = size;
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = size;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        VK_CHECK(vmaCreateBuffer(allocator_, &bci, &aci, &capture_.buffer, &capture_.allocation, &info));
        capture_.mapped = info.pMappedData;
    }

    void readCapture() {
        VK_CHECK(vmaInvalidateAllocation(allocator_, capture_.allocation, 0, VK_WHOLE_SIZE));
        Image img;
        img.width = static_cast<int>(extent_.width);
        img.height = static_cast<int>(extent_.height);
        img.rgba.resize(static_cast<size_t>(capture_.size));
        std::memcpy(img.rgba.data(), capture_.mapped, img.rgba.size());
        const bool bgra = surfaceFormat_.format == VK_FORMAT_B8G8R8A8_UNORM || surfaceFormat_.format == VK_FORMAT_B8G8R8A8_SRGB;
        for (size_t i = 0; i < img.rgba.size(); i += 4) {
            if (bgra) std::swap(img.rgba[i], img.rgba[i + 2]);
            img.rgba[i + 3] = 255;
        }
        captured_ = std::move(img);
    }

    void shutdown() {
        if (device_) {
            vkDeviceWaitIdle(device_);
            for (Frame& f : frames_) {
                releaseRetired(f);
                destroyBuffer(f.stream);
                if (f.fence) vkDestroyFence(device_, f.fence, nullptr);
                if (f.imageAvailable) vkDestroySemaphore(device_, f.imageAvailable, nullptr);
                if (f.pool) vkDestroyCommandPool(device_, f.pool, nullptr);
            }
            for (Texture& t : textures_) destroyTextureNow(t);
            destroyBuffer(capture_);
            destroySwapchainResources();
            if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
            if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
            if (descriptorPool_) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
            if (setLayout_) vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
            if (linearSampler_) vkDestroySampler(device_, linearSampler_, nullptr);
            if (nearestSampler_) vkDestroySampler(device_, nearestSampler_, nullptr);
            if (uploadFence_) vkDestroyFence(device_, uploadFence_, nullptr);
            if (uploadPool_) vkDestroyCommandPool(device_, uploadPool_, nullptr);
            if (allocator_) vmaDestroyAllocator(allocator_);
            vkDestroyDevice(device_, nullptr);
        }
        if (surface_) SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
        if (messenger_) vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
        if (instance_) vkDestroyInstance(instance_, nullptr);
    }

    SDL_Window* window_;
    bool vsync_;
    std::string deviceName_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    bool debugUtils_ = false;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = nullptr;
    VkCommandPool uploadPool_ = VK_NULL_HANDLE;
    VkFence uploadFence_ = VK_NULL_HANDLE;

    VkSampler linearSampler_ = VK_NULL_HANDLE;
    VkSampler nearestSampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    VkSurfaceFormatKHR surfaceFormat_{};
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    int requestedWidth_ = 0;  // window pixel size the swapchain was created for
    int requestedHeight_ = 0;
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainViews_;
    std::vector<VkSemaphore> renderFinished_;
    bool swapchainDirty_ = false;
    bool swapchainSupportsCopy_ = false;
    bool captureFormatOk_ = false;  // capture handles 8-bit RGBA/BGRA only

    std::array<Frame, kFramesInFlight> frames_{};
    uint32_t frameIndex_ = 0;
    uint32_t imageIndex_ = 0;
    bool inFrame_ = false;
    VkDescriptorSet boundTexture_ = VK_NULL_HANDLE;

    std::vector<Texture> textures_;
    std::vector<uint32_t> freeTextureSlots_;
    TextureId whiteTexture_;

    bool captureRequested_ = false;
    Buffer capture_;
    std::optional<Image> captured_;
};

} // namespace

std::unique_ptr<Device> createVulkanDevice(SDL_Window* window, const DeviceOptions& options, std::string& error) {
    auto device = std::make_unique<VulkanDevice>(window, options);
    try {
        device->init(options.validation);
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;  // destructor releases whatever was created
    }
    return device;
}

} // namespace opense4::gfx
