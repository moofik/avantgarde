# UI Architecture (Milestone 7 Foundation)

## Purpose
Define a separable UI architecture where each screen is an isolated widget:
- `Tracks`
- `Manager`
- `FxList`
- `FxEditor`

The goal is to prevent one large renderer/controller class and keep RT-safe command flow unchanged.

## Design Principles
1. Separation of concerns:
   - widget draws and handles local UI input
   - scene host routes scenes and global keys
   - dispatcher converts UI intents to control/runtime commands
2. RT safety:
   - widgets never call audio runtime directly
   - all runtime actions go through existing control path (`ControlCommandDispatcher` -> RT queue)
3. Incremental migration:
   - keep current `UiState` and renderer backends
   - move behavior scene-by-scene without breaking existing controls

## Existing Runtime/UI Contracts (stay stable)
- Runtime state DTO: `UiState` (`src/contracts/IUi.h`)
- Input source: `IUiGestureInput` (`src/contracts/IUiGestureInput.h`)
- Current renderer contract: `IUiRenderer::render(const UiState&)`

These remain valid while scene/widget layer is introduced.

## New UI Layer Contracts

### 1) Navigation/UI-only state
```cpp
enum class UiScene : uint8_t {
    Tracks = 0,
    Manager,
    FxList,
    FxEditor
};

struct UiNavState {
    UiScene scene{UiScene::Tracks};
    uint8_t selectedTrack{0};   // 0..1

    // Generic list navigation
    uint16_t cursor{0};
    uint16_t scroll{0};

    // Scene-specific
    uint16_t selectedFx{0};
    std::string managerCwd{"."};
    std::string managerFilter;
};
```

### 2) Intents (widget -> app actions)
```cpp
enum class UiIntentType : uint8_t {
    None = 0,
    OpenScene,
    Back,
    LoadSampleToTrack,
    AddFxToTrack,
    RemoveFxFromTrack,
    OpenFxEditor,
    SetFxParam,
    EnginePlayTrack,
    EngineStopTrack,
    EngineSetQuant,
    EngineSetBpm,
    EngineSetTrackSpeed
};

struct UiIntent {
    UiIntentType type{UiIntentType::None};
    uint8_t track{0};
    uint8_t fxSlot{0};
    uint16_t paramIndex{0};
    float value{0.0f};
    std::string path;
};
```

### 3) Widget interface
```cpp
struct WidgetOutput {
    bool handled{false};
    std::vector<UiIntent> intents;
};

struct IUiWidget {
    virtual ~IUiWidget() = default;
    virtual const char* id() const noexcept = 0;
    virtual void render(TextCanvas& canvas,
                        const UiState& rtState,
                        const UiNavState& nav) = 0;
    virtual WidgetOutput onGesture(UiGesture action,
                                   const UiState& rtState,
                                   UiNavState& nav) = 0;
};
```

### 4) Scene host/router
```cpp
class UiSceneHost {
public:
    bool registerWidget(UiScene scene, std::unique_ptr<IUiWidget> widget);
    bool renderActive(UiTextBuffer& out, const UiState& rtState) const;
    WidgetOutput handleGesture(UiGesture action, const UiState& rtState);
    UiNavState& nav() noexcept;
    const UiNavState& nav() const noexcept;
};
```

Responsibilities:
- owns active scene and `UiNavState`
- handles global keys (`q`, `1/2`, `Esc`, scene toggles)
- delegates scene-local keys to active widget
- routes Active Action Pointer in two режимах:
  - `Scope::Scene` -> action catalog активного виджета
  - `Scope::Global` -> host-level global action catalog

## Services (non-RT)

### File manager service
```cpp
struct FileEntry {
    std::string name;
    std::string path;
    bool isDir{false};
};

struct IFileBrowserService {
    virtual ~IFileBrowserService() = default;
    virtual std::vector<FileEntry> list(const std::string& cwd,
                                        const std::string& filterExt) = 0;
};
```

### Clip loading service
```cpp
struct IClipLoadService {
    virtual ~IClipLoadService() = default;
    virtual bool loadToTrackSlot(uint8_t track, uint8_t slot,
                                 const std::string& path) = 0;
};
```

`ManagerWidget` uses these services via intents, not direct runtime calls.

## Widget Responsibilities

### TracksWidget
- default scene
- display transport + 2 tracks
- local actions: play/stop/speed/quant/bpm
- can open `Manager` and `FxList`

### ManagerWidget
- list dirs/files (`wav`, `aiff`, `flac` first)
- navigation + select
- `Enter` emits `LoadSampleToTrack(track, slot=0, path)`

### FxListWidget
- per-active-track FX slots
- add/remove/select effect
- open `FxEditor` for selected slot

### FxEditorWidget
- parameter list for selected FX
- edits emit `SetFxParam(track, fxSlot, paramIndex, value)`

## Input Routing Model
1. `IUiGestureInput::poll()` returns `UiGesture`
2. `UiSceneHost::handleGesture()`:
   - handle global action if matched
   - else forward to active widget
3. Collected intents are applied by `UiIntentDispatcher`
4. Dispatcher calls:
   - UI-only changes -> `UiNavState` / store
   - engine changes -> `ControlCommandDispatcher` / clip-load service

## Rendering Model
1. Active widget renders into `TextCanvas` (logical screen buffer)
2. Backend renderer converts `TextCanvas` to concrete output:
   - terminal (`--ui=gb`)
   - macOS window (`--ui=gb-window`)
   - future SPI display backend

This keeps scene logic backend-agnostic.

## Migration Plan (safe incremental)
1. Introduce contracts: `UiScene`, `UiNavState`, `UiIntent`, `IUiWidget`, `UiSceneHost`
2. Move current track screen into `TracksWidget` with no behavior changes
3. Add `ManagerWidget` + file services + load-to-track flow
4. Add `FxListWidget`
5. Add first `FxEditorWidget` profiles (`Tape Delay`, `HPF`)
6. Wire tests per widget and scene transitions

## Acceptance Criteria for M7
- Scene switching works: `Tracks <-> Manager <-> FxList <-> FxEditor`
- File manager loads samples into both tracks via existing non-RT load path
- Two different samples can be loaded and played in parallel
- Runtime command path remains RT-safe and unchanged
- No single UI class owns all scene logic
