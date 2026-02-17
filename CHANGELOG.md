# Avantgarde – Changelog

## Версия 0.0.2
**Дата:** 17.02.2026

---

## Добавлено

### 1) ITransportBridge + TransportRtSnapshot (глобальное музыкальное время)

Введена отдельная подсистема транспорта для представления глобального музыкального времени в RT.

**Новые компоненты:**
- `TransportRtSnapshot` (POD, RT-safe)
  - `playing`
  - `tsNum`, `tsDen`
  - `ppq`
  - `bpm`
  - `quant`
  - `swing`
  - `sampleTime` (RT-владение, монотонно растёт в семплах)
- `ITransportBridge`
  - Control-методы (`setTempo`, `setPlaying`, `setTimeSignature`, `setQuantize`, `setSwing`)
  - RT-метод `swapBuffers()`
  - RT-доступ к снапшоту через `rt()`
  - `advanceSampleTime(frames)` — увеличение playhead строго в RT

**Интеграция в движок:**
- Добавлен метод `IAudioEngine::setTransportBridge(ITransportBridge* t) noexcept`
- В `AudioEngine::processBlock()` транспорт вызывается в прологе:
  - `transport_->swapBuffers()`
  - `transport_->advanceSampleTime(ctx.nframes)`

---

### 2) IRtExtension (RT-хуки расширения)

Добавлен управляемый RT-hook слой для расширения аудио-пайплайна без разрастания `AudioEngine`.

**Новый интерфейс:**
- `IRtExtension`
  - `onBlockBegin(const AudioProcessContext&) noexcept`
  - `onBlockEnd(const AudioProcessContext&) noexcept`

**Интеграция в движок:**
- `IAudioEngine::addRtExtension(IRtExtension*) noexcept`
- В `AudioEngine` хранится фиксированный массив расширений (без аллокаций)
- Вызов хуков в `processBlock()`:
  - пролог (до треков)
  - эпилог (после треков)

---

### 3) IClipTrack (контракт клипового трека со слотами)

Добавлен явный контракт клиповой модели трека (слоты клипов).

**Новый интерфейс:**
- `IClipTrack : ITrack`
  - `numSlots()`
  - `loadSlotFromFile(slot, path)`
  - `clearSlot(slot)`
  - `armRecordSlot(slot, on)`
  - `setSlotLengthInBars(slot, bars)`
  - `setSlotLooping(slot, loop)`

**Принципы:**
- Slot = фиксированная ячейка клипа внутри трека
- Управление данными — вне RT
- Управление воспроизведением/записью — через RT-команды (`onRtCommand`)

---

### 4) Запись Master Out (MVP) — без tap/resample

Добавлена минимальная запись мастер-выхода без сложной маршрутизации и ресемпла.

**Новый метод:**
- `IAudioEngine::setMasterRecordSink(IRtRecordSink*) noexcept`

**Интеграция в движок:**
- В `AudioEngine::processBlock()` после треков/эпилога:
  - `masterSink_->writeBlock(ctx.out, ctx.nframes)` (если sink установлен)
- `sink == nullptr` отключает запись

---

## Обновлено / Зафиксировано

### 5) IParamBridge (атомарный своп параметров в прологе блока)

Подсистема параметров закреплена как “прологовая” синхронизация control→RT.

**Контракт:**
- `IParamBridge::swapBuffers()` вызывается строго в прологе `processBlock()`

**Порядок в AudioEngine::processBlock():**
1. pop всех RT-команд из `IRtCommandQueue`
2. `paramBridge_->swapBuffers()` (если задан)
3. `transport_->swapBuffers()` + `transport_->advanceSampleTime(ctx.nframes)` (если задан)
4. `IRtExtension::onBlockBegin(...)`
5. `track->process(...)` по всем трекам
6. `IRtExtension::onBlockEnd(...)`
7. `masterSink_->writeBlock(...)` (если задан)

---

## Тесты

- Переписаны/дополнены тесты AudioEngine под:
  - `IRtExtension` (вызовы + порядок begin/end относительно треков)
  - `setMasterRecordSink` (вызов + порядок)
  - `setTransportBridge` (swap/advance + порядок)
  - подтверждён вызов `IParamBridge::swapBuffers()` в прологе
- Catch2: все тесты проходят локально (включая большие нагрузки на assert в других тестах репозитория)

---

## Архитектурные решения

### ✔ Transport отделён от параметров
Глобальное музыкальное время (темп/размер/квантизация/playhead) живёт отдельно от `IParamBridge` и модульных параметров.

### ✔ RT-расширяемость через IRtExtension
Scheduler / step sequencer / глобальная квантизация подключаются как расширение RT-пайплайна, без внедрения логики в engine/track.

### ✔ IClipTrack как “основа девайса”
Закрепили клиповую модель трека контрактом (слоты), без преждевременного разделения на отдельные Recorder/Sampler интерфейсы.

### ✔ Без tap/resample в MVP
Сложная маршрутизация отложена. Сейчас — только master-out запись через sink.

---

## Summary

v0.0.2 делает архитектуру “музыкально осмысленной” и готовой к step sequencing:

- Появилась настоящая ось времени (`ITransportBridge` + RT playhead)
- Появились RT-хуки расширения (`IRtExtension`) как фундамент для scheduler/sequencer
- Клип-модель трека зафиксирована (`IClipTrack`)
- Добавлена запись master-out (`setMasterRecordSink`) без усложнения
- `IParamBridge` закреплён как атомарный прологовый механизм параметров

База для квантизации, клип-лупера и будущего step-секвенсора заложена без переусложнения.
