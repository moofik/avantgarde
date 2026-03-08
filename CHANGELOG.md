# Avantgarde – Changelog

## Версия 0.0.2
**Дата:** 17.02.2026

---

## Добавлено

### 1) ITransport (contracts/ITransport.h) — глобальный музыкальный транспорт

Добавлен новый контракт `ITransport.h`, содержащий интерфейс транспорта и RT-снапшот музыкального времени.

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
  - Control-методы:
    - `setTempo(...)`
    - `setPlaying(...)`
    - `setTimeSignature(...)`
    - `setQuantize(...)`
    - `setSwing(...)`
  - RT-методы:
    - `swapBuffers()`
    - `rt()`
    - `advanceSampleTime(frames)`

**Назначение:**
- Темп, размер, квантизация и playhead вынесены в отдельную подсистему.
- Глобальное музыкальное время больше не относится к параметрам модулей.
- `sampleTime` становится RT-owned источником истины.

---

### 2) Интеграция транспорта в движок

Добавлен метод:

- `IAudioEngine::setTransportBridge(ITransportBridge* t) noexcept`

**Интеграция в `AudioEngine::processBlock()`:**

В прологе блока выполняется:

1. `transport_->swapBuffers()`
2. `transport_->advanceSampleTime(ctx.nframes)`

Таким образом:
- playhead продвигается строго в RT
- transport участвует в каждом аудио-блоке

---

### 3) IRtExtension (RT-хуки расширения)

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

Назначение:
- Scheduler
- Step sequencer
- Глобальная квантизация
- Будущая синхронизация клипов

---

### 4) IClipTrack (контракт клипового трека со слотами)

Добавлен явный контракт клиповой модели трека.

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
- Подготовка к клиповому секвенсору

---

### 5) Запись Master Out (MVP) — без tap/resample

Добавлена минимальная запись мастер-выхода.

**Новый метод:**

- `IAudioEngine::setMasterRecordSink(IRtRecordSink*) noexcept`

**Интеграция:**

- После обработки треков и RT-расширений:
  - `masterSink_->writeBlock(ctx.out, ctx.nframes)` (если sink установлен)
- `sink == nullptr` отключает запись

Tap/resample и маршрутизация отложены.

---

## Порядок выполнения в AudioEngine::processBlock()

Фактический порядок RT-пайплайна после изменений:

1. pop всех RT-команд из `IRtCommandQueue`
2. `paramBridge_->swapBuffers()` (если задан)
3. `transport_->swapBuffers()` (если задан)
4. `transport_->advanceSampleTime(ctx.nframes)` (если задан)
5. `IRtExtension::onBlockBegin(...)`
6. `track->process(...)` по всем трекам
7. `IRtExtension::onBlockEnd(...)`
8. `masterSink_->writeBlock(...)` (если задан)

---

## Тесты

- Добавлены тесты для:
  - `setTransportBridge`
  - вызова `swapBuffers()` и `advanceSampleTime()`
  - порядка Transport → Extensions → Tracks → RecordSink
  - `IRtExtension` begin/end
  - `setMasterRecordSink`

Все тесты проходят локально (Catch2).

---

## Архитектурные решения

### ✔ Transport вынесен в отдельный контракт

Глобальное музыкальное время полностью отделено от:
- параметров модулей
- треков
- аудио-движка

### ✔ RT-расширяемость через IRtExtension

Step sequencer и scheduler не будут вшиты в engine —
они подключаются как RT-расширения.

### ✔ Клип-модель закреплена контрактом

`IClipTrack` формализует клиповую архитектуру устройства.

### ✔ Минимальный MVP записи

Без tap/resample и без усложнения маршрутизации.

---

## Summary

v0.0.2 переводит Avantgarde из “аудио-движка” в “музыкальную систему”:

- Появилась глобальная ось времени (`ITransport`)
- Появился фундамент для step sequencing
- Появился RT-extension слой
- Зафиксирована клиповая модель трека
- Добавлена запись master-out

Архитектура готова к внедрению scheduler / quantization / step sequencer без рефакторинга движка.
