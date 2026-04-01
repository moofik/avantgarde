#include "service/ui/ManagerWidget.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string_view>

namespace avantgarde {
namespace {

constexpr const char* kFrameTop = "╔";
constexpr const char* kFrameTopRight = "╗";
constexpr const char* kFrameMid = "╠";
constexpr const char* kFrameMidRight = "╣";
constexpr const char* kFrameBottom = "╚";
constexpr const char* kFrameBottomRight = "╝";
constexpr const char* kFrameVert = "║";
constexpr const char* kFrameH = "═";

std::string repeatToken(std::string_view token, std::size_t count) {
    std::string out;
    out.reserve(token.size() * count);
    for (std::size_t i = 0; i < count; ++i) {
        out += token;
    }
    return out;
}

} // namespace

ManagerWidget::ManagerWidget(uint16_t frameWidth) noexcept
    : frameWidth_(frameWidth) {}

const char* ManagerWidget::id() const noexcept {
    return "manager";
}

void ManagerWidget::refresh_(UiNavState& navState) {
    if (navState.managerCwd.empty()) {
        navState.managerCwd = ".";
    }
    if (loadedCwd_ == navState.managerCwd && entries_.size() > 0) {
        return;
    }

    entries_.clear();
    lastError_.clear();

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path cwd = fs::path(navState.managerCwd);
    if (!fs::exists(cwd, ec) || !fs::is_directory(cwd, ec)) {
        navState.managerCwd = ".";
        cwd = fs::path(navState.managerCwd);
    }

    std::vector<Entry> dirs;
    std::vector<Entry> files;

    for (fs::directory_iterator it(cwd, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const fs::directory_entry& de = *it;
        Entry item{};
        item.path = de.path().string();
        item.name = de.path().filename().string();
        item.isDir = de.is_directory(ec);
        if (item.name.empty()) {
            continue;
        }
        if (item.isDir) {
            dirs.push_back(std::move(item));
        } else if (isAudioFile_(item.name)) {
            files.push_back(std::move(item));
        }
    }

    if (ec) {
        lastError_ = "dir read error";
    }

    const auto byName = [](const Entry& a, const Entry& b) {
        return toLower_(a.name) < toLower_(b.name);
    };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);

    entries_.reserve(dirs.size() + files.size());
    entries_.insert(entries_.end(), dirs.begin(), dirs.end());
    entries_.insert(entries_.end(), files.begin(), files.end());

    loadedCwd_ = navState.managerCwd;
    if (entries_.empty()) {
        navState.cursor = 0;
        navState.scroll = 0;
        return;
    }
    if (navState.cursor >= entries_.size()) {
        navState.cursor = static_cast<uint16_t>(entries_.size() - 1);
    }
    const uint16_t maxScroll = static_cast<uint16_t>((entries_.size() > listRows_) ? (entries_.size() - listRows_) : 0);
    if (navState.scroll > maxScroll) {
        navState.scroll = maxScroll;
    }
}

const ManagerWidget::Entry* ManagerWidget::selected_(const UiNavState& navState) const noexcept {
    if (entries_.empty()) {
        return nullptr;
    }
    const std::size_t idx = std::min<std::size_t>(navState.cursor, entries_.size() - 1U);
    return &entries_[idx];
}

bool ManagerWidget::isAudioFile_(const std::string& name) {
    const std::string lower = toLower_(name);
    return lower.ends_with(".wav") || lower.ends_with(".aiff") || lower.ends_with(".aif") || lower.ends_with(".flac");
}

std::string ManagerWidget::toLower_(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string ManagerWidget::padRight_(const std::string& s, std::size_t width) {
    if (s.size() >= width) {
        return s.substr(0, width);
    }
    std::string out = s;
    out.append(width - s.size(), ' ');
    return out;
}

std::string ManagerWidget::trimMiddle_(const std::string& s, std::size_t width) {
    if (s.size() <= width) {
        return s;
    }
    if (width <= 3) {
        return s.substr(0, width);
    }
    const std::size_t left = (width - 3U) / 2U;
    const std::size_t right = width - 3U - left;
    return s.substr(0, left) + "..." + s.substr(s.size() - right);
}

void ManagerWidget::render(UiTextBuffer& out, const UiState&, const UiNavState& navState) {
    UiNavState nav = navState;
    refresh_(nav);

    const std::size_t width = frameWidth_ < 20 ? 20 : frameWidth_;
    const std::size_t inner = width - 2U;
    const std::size_t listWidth = inner > 4 ? inner - 4 : inner;

    out.clear();
    out.lines.reserve(static_cast<std::size_t>(listRows_) + 10U);

    out.lines.push_back(std::string(kFrameTop) + repeatToken(kFrameH, inner) + kFrameTopRight);
    const std::string title = " MANAGER T" + std::to_string(static_cast<unsigned>(nav.selectedTrack + 1U)) +
                              " AUTO:" + (autoPreview_ ? "Y" : "N") + " ";
    out.lines.push_back(std::string(kFrameVert) + padRight_(title, inner) + kFrameVert);

    const std::string cwdLine = " CWD:" + trimMiddle_(nav.managerCwd, inner > 6 ? inner - 6 : inner);
    out.lines.push_back(std::string(kFrameVert) + padRight_(cwdLine, inner) + kFrameVert);
    out.lines.push_back(std::string(kFrameMid) + repeatToken(kFrameH, inner) + kFrameMidRight);

    const std::size_t start = std::min<std::size_t>(nav.scroll, entries_.size());
    for (std::size_t row = 0; row < listRows_; ++row) {
        const std::size_t idx = start + row;
        std::string line = " ";
        if (idx < entries_.size()) {
            const Entry& e = entries_[idx];
            const bool selected = (idx == nav.cursor);
            line += selected ? "> " : "  ";
            std::string name = e.name + (e.isDir ? "/" : "");
            line += trimMiddle_(name, listWidth);
        }
        out.lines.push_back(std::string(kFrameVert) + padRight_(line, inner) + kFrameVert);
    }

    out.lines.push_back(std::string(kFrameMid) + repeatToken(kFrameH, inner) + kFrameMidRight);
    std::string status = " keys [j/k] [enter] [h] [space] [a] [esc] ";
    if (!lastError_.empty()) {
        status = " err:" + lastError_ + " ";
    }
    out.lines.push_back(std::string(kFrameVert) + padRight_(status, inner) + kFrameVert);
    out.lines.push_back(std::string(kFrameBottom) + repeatToken(kFrameH, inner) + kFrameBottomRight);
}

WidgetOutput ManagerWidget::onGesture(UiGesture action, const UiState&, UiNavState& navState) {
    refresh_(navState);
    WidgetOutput out{};
    auto emitPreviewCurrent = [&]() {
        const Entry* e = selected_(navState);
        if (!e || e->isDir) {
            return;
        }
        UiIntent preview{};
        preview.type = UiIntentType::PreviewRequest;
        preview.track = navState.selectedTrack;
        preview.path = e->path;
        out.intents.push_back(std::move(preview));
    };

    switch (action) {
        case UiGesture::ListUp:
            if (!entries_.empty() && navState.cursor > 0) {
                --navState.cursor;
                if (navState.cursor < navState.scroll) {
                    navState.scroll = navState.cursor;
                }
                if (autoPreview_) {
                    emitPreviewCurrent();
                }
            }
            out.handled = true;
            break;
        case UiGesture::ListDown:
            if (!entries_.empty() && (navState.cursor + 1U) < entries_.size()) {
                ++navState.cursor;
                if (navState.cursor >= navState.scroll + listRows_) {
                    navState.scroll = static_cast<uint16_t>(navState.cursor + 1U - listRows_);
                }
                if (autoPreview_) {
                    emitPreviewCurrent();
                }
            }
            out.handled = true;
            break;
        case UiGesture::ListParent: {
            namespace fs = std::filesystem;
            fs::path cwd(navState.managerCwd);
            if (cwd.has_parent_path()) {
                navState.managerCwd = cwd.parent_path().string();
                navState.cursor = 0;
                navState.scroll = 0;
                loadedCwd_.clear();
                refresh_(navState);
                if (autoPreview_) {
                    UiIntent stop{};
                    stop.type = UiIntentType::PreviewStop;
                    out.intents.push_back(std::move(stop));
                }
            }
            out.handled = true;
        } break;
        case UiGesture::ListEnter: {
            const Entry* e = selected_(navState);
            if (!e) {
                out.handled = true;
                break;
            }
            if (e->isDir) {
                navState.managerCwd = e->path;
                navState.cursor = 0;
                navState.scroll = 0;
                loadedCwd_.clear();
                refresh_(navState);
                if (autoPreview_) {
                    UiIntent stop{};
                    stop.type = UiIntentType::PreviewStop;
                    out.intents.push_back(std::move(stop));
                }
            } else {
                UiIntent load{};
                load.type = UiIntentType::LoadSampleToTrack;
                load.track = navState.selectedTrack;
                load.path = e->path;
                out.intents.push_back(std::move(load));
            }
            out.handled = true;
        } break;
        case UiGesture::PreviewPlay:
            emitPreviewCurrent();
            out.handled = true;
            break;
        case UiGesture::PreviewAutoToggle:
            autoPreview_ = !autoPreview_;
            if (autoPreview_) {
                emitPreviewCurrent();
            } else {
                UiIntent stop{};
                stop.type = UiIntentType::PreviewStop;
                out.intents.push_back(std::move(stop));
            }
            out.handled = true;
            break;
        default:
            break;
    }

    return out;
}

} // namespace avantgarde
