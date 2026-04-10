#include "service/ui/widgets/ManagerWidget.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cstdio>

namespace avantgarde {

ManagerWidget::ManagerWidget(uint16_t frameWidth,
                             std::optional<UiLayoutTemplate> layoutTemplate) noexcept
    : frameWidth_(frameWidth) {
    if (layoutTemplate.has_value()) {
        layoutTemplate_ = layoutTemplate;
        buildLayoutModel_(*layoutTemplate);
    }
}

const char* ManagerWidget::id() const noexcept {
    return "manager";
}

void ManagerWidget::refresh_(UiNavState& navState) const {
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

bool ManagerWidget::buildPreparedLayout(UiPreparedLayout& out,
                                        const UiState&,
                                        const UiNavState& navState) const {
    if (!layoutTemplate_.has_value() || !layout_.enabled) {
        return false;
    }

    // Подготовка списка файлов может менять только UI-кэш, поэтому работаем с
    // локальной копией navState и не мутируем исходный стейт хоста.
    UiNavState nav = navState;
    refresh_(nav);

    const uint16_t frameWidth = std::max<uint16_t>(frameWidth_, 20U);
    const std::size_t inner = static_cast<std::size_t>(frameWidth - 2U);
    const std::size_t nameWidth = (inner > 8U) ? (inner - 8U) : inner;

    std::vector<std::string> rows{};
    rows.reserve(listRows_);
    int32_t selectedRow = -1;
    const std::size_t start = std::min<std::size_t>(nav.scroll, entries_.size());
    for (std::size_t row = 0; row < listRows_; ++row) {
        const std::size_t idx = start + row;
        if (idx >= entries_.size()) {
            rows.emplace_back(" ");
            continue;
        }
        const Entry& e = entries_[idx];
        std::string line = e.isDir ? "[D] " : "[F] ";
        line += trimMiddle_(e.name + (e.isDir ? "/" : ""), nameWidth);
        rows.push_back(std::move(line));
        if (idx == nav.cursor) {
            selectedRow = static_cast<int32_t>(row);
        }
    }
    if (selectedRow < 0 && !rows.empty() && !entries_.empty()) {
        selectedRow = 0;
    }

    char title[196]{};
    std::snprintf(title,
                  sizeof(title),
                  " %s T%u AUTO:%c ",
                  layout_.title.c_str(),
                  static_cast<unsigned>(nav.selectedTrack + 1U),
                  autoPreview_ ? 'Y' : 'N');
    const std::size_t cwdAvail = (inner > 6U) ? (inner - 6U) : inner;
    const std::string cwdLine = " CWD:" + trimMiddle_(nav.managerCwd, cwdAvail);

    std::string status = " status: ready ";
    if (!lastError_.empty()) {
        status = " err:" + lastError_ + " ";
    }
    const std::string keys = !layout_.keysHint.empty()
                                 ? layout_.keysHint
                                 : " keys [j/k] [enter/open] [h/up] [space preview] [a auto] [esc back] ";

    UiPreparedLayoutBuilder builder{};
    builder.sceneId("manager")
        .templateRef(&(*layoutTemplate_))
        .frameWidth(frameWidth)
        .frameHeightHint(static_cast<uint16_t>(6U + listRows_))
        .addComponent(UiStatusBarBuilder("header_title").text(title))
        .addComponent(UiTextBuilder("cwd_line").text(std::move(cwdLine)))
        .addComponent(UiSeparatorBuilder("sep_top").style(UiSeparatorComponent::Style::Heavy))
        .addComponent(UiListBuilder("manager_list")
                          .rows(std::move(rows))
                          .selectedRow(selectedRow)
                          .marker(UiListComponent::Marker::Arrow))
        .addComponent(UiSeparatorBuilder("sep_bottom").style(UiSeparatorComponent::Style::Heavy))
        .addComponent(UiTextBuilder("status_line").text(std::move(status)))
        .addComponent(UiTextBuilder("keys_hint").text(keys));

    out = std::move(builder).build();
    return true;
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

void ManagerWidget::collectNodes_(const UiLayoutNode& root, std::vector<const UiLayoutNode*>& out) noexcept {
    out.push_back(&root);
    for (const UiLayoutNode& child : root.children) {
        collectNodes_(child, out);
    }
}

void ManagerWidget::buildLayoutModel_(const UiLayoutTemplate& tpl) {
    layout_ = LayoutModel{};
    if (tpl.widgetId != "manager") {
        return;
    }
    std::vector<const UiLayoutNode*> nodes{};
    collectNodes_(tpl.root, nodes);
    for (const UiLayoutNode* node : nodes) {
        if (!node) {
            continue;
        }
        if (node->type == UiLayoutNodeType::StatusBar && !node->text.empty()) {
            layout_.title = node->text;
        }
        if (node->type == UiLayoutNodeType::Text &&
            node->id == "keys_hint" &&
            !node->text.empty()) {
            layout_.keysHint = node->text;
        }
    }
    layout_.enabled = true;
}

} // namespace avantgarde
