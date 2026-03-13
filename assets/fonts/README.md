# Gothic Core Fonts

Drop any `.ttf` / `.otf` font files into this folder for `--ui=gb-window`.

Loading behavior:

- Renderer auto-discovers all font files in this folder.
- First discovered font is used for header/title.
- Body font uses a fixed-pitch font from discovered files when available.
- If no suitable body font is found, renderer falls back to built-in monospaced fonts.

You can keep just one file here (for example `gothic-pixel-font.ttf`).
