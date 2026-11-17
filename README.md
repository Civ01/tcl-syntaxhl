# tcl-syntaxhl

A Total Commander **Lister plugin** (64-bit) that shows source and data files with
read-only syntax highlighting. It works in Lister (F3) and in Quick View (Ctrl+Q).

Supported out of the box: **JSON** (optionally JSONC), **CSS** (optionally SCSS/LESS),
**Python**, **JavaScript** and **INI**. You can add more languages with a new native
lexer or with a `.rules` file. See [specs.md](specs.md) for the full specification.

## Installation

1. Download `tcl-syntaxhl-<version>-x64.zip` from
   [Releases](https://github.com/Civ01/tcl-syntaxhl/releases). Builds of every
   commit are attached to each CI run as an artifact (GitHub → Actions → CI). You
   can also build it yourself (see below).
2. Open the zip in Total Commander (x64). TC detects `pluginst.inf` and offers to
   install the plugin. Alternatively, copy the folder anywhere and add
   `tcl-syntaxhl.wlx64` under *Configuration → Options → Plugins → Lister (WLX)*.
3. Keep `tcl-syntaxhl.ini`, `themes\` and `rules\` next to the DLL.

Requirements: Windows 10 or later, Total Commander x64. Dark-mode following needs TC 11+.

## Using it

| Key / action | Effect |
|---|---|
| F7, Ctrl+F / F3 (find next) | Lister search, with case, whole-word and backwards options |
| Ctrl+C, Ctrl+A | Copy the selection, select all |
| Ctrl+G | Go to line (`123` or `123:45` for line:column) |
| Ctrl+Mouse wheel | Zoom (per window, not saved) |
| Lister "Wrap text" option | Word wrap on/off |
| Right click | Context menu (Copy, Select all, Go to line) |

Other keys (Esc, N/P, 1–7, …) are passed on to Lister. Find-first starts at the
first displayed line, as the TC plugin documentation specifies.

If you choose *Image/Multimedia* from Lister's menu (`lcp_forceshow`), files with
extensions the plugin doesn't know are shown as plain text instead of being refused.

## Configuration: `tcl-syntaxhl.ini`

The INI file is fully commented. The important settings are:

```ini
[General]
tokenizer=native          ; native | regex
max_highlight_size=2M     ; larger files are not fully highlighted
use_lazy_highlight=false  ; true: highlight only the visible part of large files
theme=auto                ; auto (follow TC dark mode) or a folder under themes\
theme_light=light
theme_dark=dark
font_face=Consolas
font_size=10
italic_comments=true      ; false: never italic (also per language: [INI] italic_comments=false)
tab_width=4               ; columns per tab character (display only; per language too)
allow_beeps=false         ; RichEdit beeps at the start/end of the file (Lister is silent)

[Languages]               ; LanguageName=extensions
JSON=json
CSS=css
Python=py,pyw
JS=js,mjs,cjs
INI=ini,cfg,inf

[JSON]
support_jsonc_comments=false
[CSS]
support_scss_less=false
```

Changes take effect the next time a file is opened. The extension list is the one
exception (see below).

### Changing file extensions

Total Commander asks a plugin for its *detect string* (the list of extensions it
handles) only when the plugin is installed, and then caches it. After you edit
`[Languages]`, do one of the following:

- Remove the plugin in *Options → Plugins → Lister* and add it again.
- Edit the detect string by hand in the same dialog, e.g. append `| EXT="SCSS"`.

### Large files

- Files up to `max_highlight_size` are fully highlighted.
- Larger files are shown as plain text, with a notice bar at the top that explains why.
- With `use_lazy_highlight=true`, larger files are highlighted as you scroll instead
  (only the visible area plus a margin).

### Adding a language that reuses a lexer

```ini
[Languages]
JSONC=jsonc,code-workspace
[JSONC]
lexer=json
support_jsonc_comments=true
```

## Themes

A theme set is a folder under `themes\`. Inside it, `default.theme` holds the base
palette, and `<language>.theme` files hold only the overrides for one language. The
language name is the lowercase key from `[Languages]`: for example, `python.theme`.

```ini
[View]
background=#FFFFFF
foreground=#000000
infobar_background=#FFF4CE
infobar_foreground=#000000

[Tokens]
; class=foreground[,background][,flags]   flags: bold, italic, underline (combine with +), normal
keyword=#0000FF
comment=#008000,,italic
error=#FF0000,#FFE0E0,underline
section=#800000,,normal       ; flags given -> they replace the inherited ones (not bold)
section_bracket=@section      ; "@class": look exactly like another class
```

How values are resolved:

- **Empty or omitted fields keep the inherited value.** `section=#FF0000` changes
  only the color and stays bold; `section=#FF0000,,normal` also removes the bold.
- **Missing entries** fall back to `default.theme`, then to the built-in palette.
  Some classes have a parent and use the parent's style unless a theme sets them:
  `section_bracket` uses `punctuation`.
- **Unknown class names are ignored.**
- **`italic_comments=false`** in `tcl-syntaxhl.ini` removes italic from comments,
  whatever the theme says. It can also be set per language.

INI section headers are colored in two parts: `section` is the name, and
`section_bracket` is the `[` and `]`.

### Token classes

`default`, `keyword`, `builtin`, `constant`, `identifier`, `function`, `type`, `decorator`,
`number`, `string`, `string_escape`, `comment`, `operator`, `punctuation`, `key`,
`section`, `value`, `selector`, `property`, `at_rule`, `variable`, `regex`,
`template`, `error`, `section_bracket`.

## Regex tokenizer (`.rules` files)

With `tokenizer=regex` (globally, or per language as `[JSON] tokenizer=regex`), a
language is tokenized by `rules\<language>.rules`. Languages without a rules file
fall back to the native lexer, and a notice explains this. A language with a rules
file but no native lexer works too. `json.rules`, `ini.rules`, `css.rules` and
`js.rules` ship and double as references for the format. They are deliberately
simpler than the native lexers: no SCSS/LESS, and in JS a `/` starts a regex only
after punctuation or at the start of a line.

```ini
[Rules]                    ; initial state
10=comment:/\*             ; N=class:regex, tried in ascending N
10.push=block_comment      ; switch state after the match
20=string:"(?:\\.|[^"\\])*"

[Rules.block_comment]
default=comment            ; class for unmatched characters
10=comment:\*/
10.pop=true                ; back to the previous state
; eol_pop=true             ; (optional) the state ends with the line
```

The regexes use ECMAScript syntax (`std::regex`) and match within one line, so `^`
matches only at the start of a line and `$` only at its end. The rules engine
doesn't see INI options such as `support_jsonc_comments`, so `json.rules` always
colors comments as comments.

## Building

The build needs CMake ≥ 3.20 and Visual Studio 2022 (MSVC v143), or MinGW-w64 for cross builds.

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --build build --config Release --target package_zip
```

- The built DLL is placed next to a copy of `dist\`, so you can point TC straight at
  `build\Release\tcl-syntaxhl.wlx64`.
- The `package_zip` target creates `build\tcl-syntaxhl-<version>-x64.zip`.
- The core library (lexers, config, themes) is portable. On Linux, `cmake -S . -B build
  && cmake --build build && ctest --test-dir build` builds and tests only the core.
- To cross-compile the plugin with MinGW, add
  `-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake`.

### Releases

Push a version tag to publish a release, for example:

```
git tag v0.2.0 && git push origin v0.2.0
```

The `Release` workflow builds and tests the plugin with MSVC, takes the version
from the tag (it goes into the DLL's version resource and the zip name), and
publishes `tcl-syntaxhl-0.2.0-x64.zip`. Tags with a suffix, such as
`v0.2.0-beta1`, become pre-releases.

### Tests

- **`shl_tests`**: unit tests (doctest). They cover the lexers and resumability, lazy
  tokenization, the regex rules, config/INI/themes, decoding, and random-input robustness.
- **`plugin_smoke`** (Windows): loads the real DLL inside a fake `TLister` window and
  checks text, colors, search, commands, `ListLoadNext`, encodings, the large-file
  notice, lazy highlighting, the regex tokenizer and the language options.

## Project layout

```
src/core     portable: config, INI, decoder, themes, lexer selection, lazy model
src/lexers   native lexers (one file per language, registered in core/LexerFactory.cpp)
src/regex    .rules driven lexer
src/plugin   TC exports (listplug.h), Windows helpers
src/view     Lister window, RichEdit highlighter, info bar
src/ui       Go-to-line dialog, settings dialog skeleton, resources
dist/        files shipped next to the DLL
tests/       unit tests, sample files
tools/       plugin_smoke end-to-end test
```

### Adding a native lexer

1. Add `src/lexers/FooLexer.cpp`, deriving from `LineLexer`. All state that crosses
   a line boundary must be kept in `LexState`.
2. Declare its factory in `Lexers.h` and register it in `kNative` in
   `core/LexerFactory.cpp`.
3. Add `Foo=ext` to `[Languages]` in `dist/tcl-syntaxhl.ini`, plus optional
   `themes/*/foo.theme` overrides.
4. Add tests, including `checkResumable`.
