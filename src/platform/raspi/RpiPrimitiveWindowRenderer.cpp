#include "platform/raspi/RpiPrimitiveWindowRenderer.h"

#include "app/AppDiagnostics.h"
#include "platform/raspi/RpiPrimitiveScenePainter.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace avantgarde::raspi {
namespace {

constexpr RpiRgba kBgMain{11, 10, 15, 255};

#if defined(__linux__)
class LinuxFramebuffer final {
public:
    ~LinuxFramebuffer() { shutdown(); }

    bool init(const RpiUiConfig& cfg, std::string& errorOut) {
        shutdown();
        errorOut.clear();

        const char* fbPath = "/dev/fb0";
        fd_ = ::open(fbPath, O_RDWR);
        if (fd_ < 0) {
            errorOut = std::string("cannot open /dev/fb0: ") + std::strerror(errno);
            AppDiagnostics::logf(AppLogLevel::Warn, "RpiPrimitiveWindowRenderer: %s", errorOut.c_str());
            return false;
        }

        if (::ioctl(fd_, FBIOGET_FSCREENINFO, &fix_) < 0 ||
            ::ioctl(fd_, FBIOGET_VSCREENINFO, &var_) < 0) {
            errorOut = std::string("ioctl(FBIOGET_*) failed: ") + std::strerror(errno);
            AppDiagnostics::logf(AppLogLevel::Warn, "RpiPrimitiveWindowRenderer: %s", errorOut.c_str());
            shutdown();
            return false;
        }

        if (var_.xres == 0U || var_.yres == 0U || fix_.line_length == 0U) {
            errorOut = "invalid framebuffer geometry";
            AppDiagnostics::logf(AppLogLevel::Warn, "RpiPrimitiveWindowRenderer: %s", errorOut.c_str());
            shutdown();
            return false;
        }

        width_ = static_cast<uint16_t>(std::min<uint32_t>(var_.xres, 65535U));
        height_ = static_cast<uint16_t>(std::min<uint32_t>(var_.yres, 65535U));
        if (cfg.width > 0 && cfg.height > 0) {
            width_ = std::min<uint16_t>(width_, cfg.width);
            height_ = std::min<uint16_t>(height_, cfg.height);
        }

        mapSize_ = static_cast<std::size_t>(fix_.line_length) * static_cast<std::size_t>(var_.yres);
        fbMem_ = static_cast<uint8_t*>(::mmap(nullptr, mapSize_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
        if (!fbMem_ || fbMem_ == MAP_FAILED) {
            errorOut = std::string("mmap failed: ") + std::strerror(errno);
            AppDiagnostics::logf(AppLogLevel::Warn, "RpiPrimitiveWindowRenderer: %s", errorOut.c_str());
            fbMem_ = nullptr;
            shutdown();
            return false;
        }

        AppDiagnostics::logf(AppLogLevel::Info,
                             "RpiPrimitiveWindowRenderer: framebuffer ready (%ux%u, bpp=%u)",
                             static_cast<unsigned>(width_),
                             static_cast<unsigned>(height_),
                             static_cast<unsigned>(var_.bits_per_pixel));
        return true;
    }

    void shutdown() noexcept {
        if (fbMem_) {
            ::munmap(fbMem_, mapSize_);
            fbMem_ = nullptr;
        }
        mapSize_ = 0;
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        width_ = 0;
        height_ = 0;
        std::memset(&fix_, 0, sizeof(fix_));
        std::memset(&var_, 0, sizeof(var_));
    }

    [[nodiscard]] bool ready() const noexcept {
        return (fd_ >= 0) && (fbMem_ != nullptr) && width_ > 0 && height_ > 0;
    }

    [[nodiscard]] uint16_t width() const noexcept { return width_; }
    [[nodiscard]] uint16_t height() const noexcept { return height_; }

    void present(const RpiPixelCanvas& canvas, uint16_t rotateDeg) noexcept {
        if (!ready()) {
            return;
        }
        const uint16_t rotation = static_cast<uint16_t>(rotateDeg % 360U);
        const uint16_t w = std::min<uint16_t>(canvas.width(), (rotation == 90U || rotation == 270U) ? height_ : width_);
        const uint16_t h = std::min<uint16_t>(canvas.height(), (rotation == 90U || rotation == 270U) ? width_ : height_);
        const auto& src = canvas.pixels();

        auto writePixel = [&](uint16_t dx, uint16_t dy, uint8_t r, uint8_t g, uint8_t b) {
            if (dx >= width_ || dy >= height_) {
                return;
            }
            uint8_t* dstRow = fbMem_ + static_cast<std::size_t>(dy) * static_cast<std::size_t>(fix_.line_length);
            if (var_.bits_per_pixel == 16) {
                const uint16_t rr =
                    static_cast<uint16_t>((static_cast<uint32_t>(r) * ((1U << var_.red.length) - 1U)) / 255U);
                const uint16_t gg = static_cast<uint16_t>(
                    (static_cast<uint32_t>(g) * ((1U << var_.green.length) - 1U)) / 255U);
                const uint16_t bb = static_cast<uint16_t>(
                    (static_cast<uint32_t>(b) * ((1U << var_.blue.length) - 1U)) / 255U);
                const uint16_t packed = static_cast<uint16_t>(
                    (rr << var_.red.offset) |
                    (gg << var_.green.offset) |
                    (bb << var_.blue.offset));
                reinterpret_cast<uint16_t*>(dstRow)[dx] = packed;
            } else {
                const uint32_t rr =
                    (static_cast<uint32_t>(r) * ((1U << var_.red.length) - 1U) / 255U) << var_.red.offset;
                const uint32_t gg =
                    (static_cast<uint32_t>(g) * ((1U << var_.green.length) - 1U) / 255U) << var_.green.offset;
                const uint32_t bb =
                    (static_cast<uint32_t>(b) * ((1U << var_.blue.length) - 1U) / 255U) << var_.blue.offset;
                const uint32_t aa =
                    (var_.transp.length > 0U) ? (((1U << var_.transp.length) - 1U) << var_.transp.offset) : 0U;
                reinterpret_cast<uint32_t*>(dstRow)[dx] = rr | gg | bb | aa;
            }
        };

        for (uint16_t sy = 0; sy < h; ++sy) {
            const std::size_t srcRow = static_cast<std::size_t>(sy) * static_cast<std::size_t>(canvas.width());
            for (uint16_t sx = 0; sx < w; ++sx) {
                const uint32_t px = src[srcRow + static_cast<std::size_t>(sx)];
                const uint8_t r = static_cast<uint8_t>((px >> 24U) & 0xFFU);
                const uint8_t g = static_cast<uint8_t>((px >> 16U) & 0xFFU);
                const uint8_t b = static_cast<uint8_t>((px >> 8U) & 0xFFU);
                uint16_t dx = sx;
                uint16_t dy = sy;
                switch (rotation) {
                    case 90U:
                        dx = static_cast<uint16_t>(width_ - 1U - sy);
                        dy = sx;
                        break;
                    case 180U:
                        dx = static_cast<uint16_t>(width_ - 1U - sx);
                        dy = static_cast<uint16_t>(height_ - 1U - sy);
                        break;
                    case 270U:
                        dx = sy;
                        dy = static_cast<uint16_t>(height_ - 1U - sx);
                        break;
                    case 0U:
                    default:
                        break;
                }
                writePixel(dx, dy, r, g, b);
            }
        }
    }

private:
    int fd_{-1};
    uint8_t* fbMem_{nullptr};
    std::size_t mapSize_{0};
    fb_fix_screeninfo fix_{};
    fb_var_screeninfo var_{};
    uint16_t width_{0};
    uint16_t height_{0};
};
#endif

} // namespace

struct RpiPrimitiveWindowRenderer::Impl {
    RpiUiConfig config{};
    RpiPixelCanvas canvas{};
    std::string cwd{};
    bool initialized{false};
    bool warnedFallback{false};
    uint64_t frameTick{0U};
    std::chrono::steady_clock::time_point startTs{std::chrono::steady_clock::now()};
#if defined(__linux__)
    LinuxFramebuffer fb{};
#endif
};

RpiPrimitiveWindowRenderer::RpiPrimitiveWindowRenderer()
    : impl_(std::make_unique<Impl>()) {}

RpiPrimitiveWindowRenderer::~RpiPrimitiveWindowRenderer() = default;

bool RpiPrimitiveWindowRenderer::init(const RpiUiConfig& config, std::string& errorOut) {
    if (!impl_) {
        errorOut = "renderer impl is null";
        return false;
    }
    impl_->config = config;
    impl_->frameTick = 0U;
    impl_->startTs = std::chrono::steady_clock::now();
    impl_->warnedFallback = false;
    try {
        impl_->cwd = std::filesystem::current_path().string();
    } catch (...) {
        impl_->cwd.clear();
    }

    if (config.headless) {
        impl_->canvas.resize(config.width, config.height);
        impl_->initialized = true;
        AppDiagnostics::logf(AppLogLevel::Info,
                             "RpiPrimitiveWindowRenderer: headless canvas %ux%u",
                             static_cast<unsigned>(impl_->canvas.width()),
                             static_cast<unsigned>(impl_->canvas.height()));
        errorOut.clear();
        return true;
    }

#if defined(__linux__)
    if (!impl_->fb.init(config, errorOut)) {
        impl_->initialized = false;
        return false;
    }
    const bool swapAxes = (config.rotateDeg == 90U || config.rotateDeg == 270U);
    impl_->canvas.resize(swapAxes ? impl_->fb.height() : impl_->fb.width(),
                         swapAxes ? impl_->fb.width() : impl_->fb.height());
    impl_->initialized = true;
    return true;
#else
    errorOut = "framebuffer output is supported only on Linux";
    impl_->initialized = false;
    return false;
#endif
}

void RpiPrimitiveWindowRenderer::render(const UiState& state) {
    (void)state;
    if (!impl_) {
        return;
    }
    if (!impl_->warnedFallback) {
        AppDiagnostics::logf(AppLogLevel::Info,
                             "RpiPrimitiveWindowRenderer: waiting for prepared-layout frames");
        impl_->warnedFallback = true;
    }
}

void RpiPrimitiveWindowRenderer::renderPreparedLayout(const UiPreparedLayout& prepared) noexcept {
    if (!impl_ || !impl_->initialized) {
        return;
    }
    impl_->canvas.clear(kBgMain);
    ++impl_->frameTick;

    RpiPrimitiveScenePaintContext ctx{};
    ctx.canvas = &impl_->canvas;
    ctx.frameTick = impl_->frameTick;
    ctx.startTs = impl_->startTs;
    ctx.cwd = impl_->cwd;
    renderPreparedLayoutScene(ctx, prepared);

#if defined(__linux__)
    if (!impl_->config.headless) {
        impl_->fb.present(impl_->canvas, impl_->config.rotateDeg);
    }
#endif
}

} // namespace avantgarde::raspi
