# UI JSON Style Guide

Краткий гайд по текущей UI-схеме (`themes.json`, `styles.json`, `effects.json`, `layouts/*.json`).

## Быстрый ответ
- `gap` — расстояние **между дочерними элементами** контейнера (`row`/`column`).
- `wrap` — перенос элементов на новую строку (актуально для `row`):
  - `true`: можно переносить;
  - `false`: все остаются в одной строке.

## Где что лежит
- `assets/ui/themes.json` — токены темы (цвета, шрифты, размеры).
- `assets/ui/styles.json` — переиспользуемые стили (`style_ref`).
- `assets/ui/effects.json` — пресеты визуальных эффектов.
- `assets/ui/layouts/*.json` — экраны (структура и компоненты).

## Основные поля ноды
- `type` — тип компонента (`text`, `statusbar`, `row`, `column`, `knob`, `icon`, `list`, ...).
- `id` — уникальный id в рамках layout.
- `style_ref` — ссылка на стиль, например `@styles.track.icon`.
- `bind` — что показывать (`read`), например `track.selected.icon.arm`.
- `target` — что менять (`write`), например `param.track.selected.speed`.
- `children` — вложенные элементы.

## Размеры и отступы
- `width` / `height`:
  - число: фиксированный размер (px/ячейки),
  - `"100%"`: процент от контейнера,
  - `"auto"`: размер по контенту/лейауту.
- `margin` — внешний отступ ноды.
- `padding` — внутренний отступ ноды.
- `gap` — промежуток между соседними `children`.

## Контейнеры (`row` / `column`)
- `justify` — распределение по главной оси:
  - `start`, `center`, `end`, `space_between`.
- `align` — выравнивание по поперечной оси:
  - `start`, `center`, `end`.
- `wrap` (только `row`) — перенос элементов на следующую строку.

### Про `justify: "center"`
Поддерживается и работает для `row`.

Важно: `center` заметен только если в строке есть свободное место.
Если сумма ширин детей уже равна `100%`, визуально центрирования не будет
(нечего "сдвигать" к центру).

Пример:
```json
{
  "type": "row",
  "width": "100%",
  "justify": "center",
  "gap": 1,
  "children": [
    { "type": "text", "id": "a", "width": "20%" },
    { "type": "text", "id": "b", "width": "20%" }
  ]
}
```
Здесь 40% занято контентом, остальное пространство будет симметрично по краям.

## Состояния компонента
У любой ноды можно задавать:
- `active`
- `inactive`
- `disabled`

Внутри state-блока:
- `if` — условие (`track.selected.arm.enabled`, `!track.selected.exists`, и т.д.).
- `text_color`, `border_color`, `background_color`
- `font`, `font_size`, `knob_size`
- `opacity` (0..1)
- `effects` (локальные эффекты состояния)

Важно:
- Если `inactive.opacity` не задан — берется дефолт движка (приглушенный вид).
- Если нужно, чтобы `inactive` был ярким, явно ставь `"opacity": 1.0`.

## Видимость и доступность
- `visible_if` — скрыть/показать компонент полностью.
- `active/inactive/disabled` — визуальное состояние, не обязательно скрывает компонент.

## Цвета и шрифты
- Цвета лучше задавать через тему:
  - `@theme.colors.text.primary`
  - `@theme.colors.text.record_active`
- Шрифты через тему:
  - `@theme.fonts.display`, `@theme.fonts.medieval`, ...

## Эффекты
Нода может иметь:
- `effects`: `[{"preset":"@effects.glitch.header.tracks"}]`
или state-специфичные эффекты в `active.effects` / `inactive.effects` / `disabled.effects`.

## Мини-пример
```json
{
  "type": "row",
  "id": "track_ux_row",
  "width": "100%",
  "gap": 0,
  "wrap": false,
  "justify": "space_between",
  "children": [
    {
      "type": "text",
      "id": "icon_arm",
      "bind": "track.selected.icon.arm",
      "style_ref": "@styles.track.icon",
      "active": {
        "if": "track.selected.recording.armed",
        "text_color": "@theme.colors.text.record_active"
      },
      "inactive": {
        "if": "track.selected.arm.enabled",
        "text_color": "@theme.colors.text.arm_on",
        "opacity": 1.0
      },
      "disabled": {
        "if": "!track.selected.arm.enabled",
        "text_color": "@theme.colors.text.arm_off"
      }
    }
  ]
}
```
