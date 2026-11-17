# tcl-syntaxhl — Specification

Total Commander Lister plugin (64-bit) that displays source/data files with read-only syntax highlighting.

- **Status:** v1 spec, drafted from the requirements interview on 2026-11-17
- **Repo:** https://github.com/Civ01/tcl-syntaxhl
- **Binary name:** `tcl-syntaxhl.wlx64`

---

## 1. Goals and non-goals

### 1.1 Goals (v1)

1. Highlight **JSON, CSS, Python, JavaScript and INI** files inside TC Lister (F3) and Quick View (Ctrl+Q).
2. Use an architecture where adding a language is a small, local change: one lexer class plus registry entry, or one `.rules` file.
3. Provide **two tokenizer backends** behind one interface:
   - **native**: hand-written lexers. This is the default.
   - **regex**: a data-driven engine driven by `.rules` files. v1 ships one rule set (`json.rules`) for testing.
4. Configure everything through `tcl-syntaxhl.ini` next to the DLL. Colors come from theme sets in `themes/`.
5. Handle large files with a configurable size limit and optional lazy (visible-area) highlighting.
6. Support basic viewer features: Find, Copy/Select all, word wrap, Ctrl+wheel zoom, and Ctrl+G go-to-line.
7. Put a backbone in place for a future runtime configuration window (a settings model and a dialog skeleton, not wired into the UI in v1).

### 1.2 Non-goals (v1)

- Editing of any kind.
- Printing (`ListPrint`). Planned for a later version.
- Line-number gutter.
- 32-bit (`.wlx`) build.
- Windows versions older than 10.
- TypeScript/JSX keywords, Python f-string internals, semantic analysis, folding, bracket matching.

---

## 2. Platform and build

| Item | Decision |
|---|---|
| Language | C++20, MSVC (VS2022 toolset v143 / 14.4x) |
| Target | Windows 10+, x64 only, TC 9.x+ (dark-mode support needs TC 11+) |
| Build system | CMake ≥ 3.20 (opens directly in VS2022) |
| CRT | Static (`/MT`), so no VC++ redistributable is needed |
| Output | `tcl-syntaxhl.wlx64` (DLL with `.wlx64` suffix, exports via `.def`) |
| Display control | RichEdit 4.1+ (`Msftedit.dll`, class `RICHEDIT50W`), read-only |
| External deps | None at runtime. Tests use a vendored single-header framework (doctest). |

### 2.1 Portability split

- **Core** (`src/core`, `src/lexers`, `src/regex`) is pure, portable C++ with no `<windows.h>` dependency. It builds and runs its unit tests on Linux/GCC and on MSVC.
- **Platform** (`src/plugin`, `src/view`, `src/ui`) holds the WinAPI code: TC exports, windows, RichEdit, and dialogs.

### 2.2 CI (GitHub Actions)

- `windows-latest`: configure and build with MSVC (Release x64), run the unit tests, and package the release zip as a workflow artifact.
- `ubuntu-latest`: build the core and run the unit tests. This is a fast feedback loop and keeps the core portable.

### 2.3 Distribution package (zip)

```
tcl-syntaxhl.wlx64
tcl-syntaxhl.ini        ; default config, fully commented
pluginst.inf            ; TC auto-install descriptor
themes/light/*.theme
themes/dark/*.theme
rules/json.rules
README.md
```

`pluginst.inf`:
```ini
[plugininstall]
description=Syntax highlighting viewer (JSON, CSS, Python, JS, INI)
type=wlx
file=tcl-syntaxhl.wlx
defaultdir=tcl-syntaxhl
```
TC x64 installs the `.wlx64` file with the same base name as `file=`, so the entry names `.wlx` even though only the `.wlx64` ships. This still needs verification with a real TC install.

---

## 3. Total Commander Lister API

Exported functions are Unicode-first. The ANSI variants convert their input and delegate to the Unicode ones.

| Export | Behaviour |
|---|---|
| `ListGetDetectString` | Builds the detect string from the `[Languages]` extension map in the INI, e.g. `EXT="JSON" \| EXT="PY" \| EXT="PYW" \| ...`. It is truncated safely to `maxlen`. TC caches this string at install time, so the README must explain how to refresh it after the extension map changes. |
| `ListSetDefaultParams` | Stores the interface version. The plugin INI path is derived from the DLL location (`GetModuleFileNameW` → `tcl-syntaxhl.ini`), not from `DefaultIniName`. |
| `ListLoadW` / `ListLoad` | Resolves the file's extension to a language. If it is unmapped, returns `NULL` so TC falls through to the next plugin. Otherwise it creates the container window, loads, decodes and highlights the file, and returns the HWND. |
| `ListLoadNextW` / `ListLoadNext` | Reuses the existing window when Lister moves to the next or previous file. Returns `LISTPLUGIN_ERROR` for unmapped files. |
| `ListCloseWindow` | Destroys the window and frees per-window state. |
| `ListSearchTextW` / `ListSearchText` | Finds text through RichEdit `EM_FINDTEXTEXW`, honouring `lcs_findfirst`, `lcs_matchcase`, `lcs_wholewords` and `lcs_backwards`. Selects and scrolls to the match. Returns `LISTPLUGIN_ERROR` when nothing is found, and TC shows its own message. |
| `ListSendCommand` | `lc_copy` copies the selection (or nothing). `lc_selectall` selects all text. `lc_newparams` applies the wrap flag (`lcp_wraptext`) and dark mode (`lcp_darkmode`). `lc_setpercent` scrolls to that position. |
| `ListPrint` | Not exported in v1. |
| `ListNotificationReceived` | Not needed in v1. |

### 3.1 Lister integration rules

- **Scroll position:** report percent changes to Lister with `PostMessage(parent, WM_COMMAND, MAKELONG(percent, itm_percent), hwnd)`.
- **Key forwarding:** keys the plugin does not handle (Esc, N/P, 1–7, F-keys, etc.) are forwarded to the Lister parent window, so Lister hotkeys keep working.
- **Quick View:** the plugin must not steal focus in Quick View mode (`lcp_forceshow` / when the parent is not a Lister window).
- **Concurrency:** several Lister windows can be open at once. All state is per window, and there are no mutable globals except a read-only cache of the immutable DLL path.
- **Config reload:** the INI, themes and rules are re-read on every `ListLoad`/`ListLoadNext`. They are cheap to read, and this means edits apply without restarting TC.

---

## 4. Architecture

```
TC  ──►  plugin/exports.cpp  (C ABI, ANSI→W shims)
              │
              ▼
        view/ListerWindow ── owns ──► InfoBar (top strip, hidden unless needed)
              │                      ► RichEdit (read-only text view)
              │                      ► GotoLineDialog (Ctrl+G)
              │
              ├── core/Config      (INI model: general, languages, per-language options)
              ├── core/TextDecoder (bytes → UTF-16, encoding detection)
              ├── core/LexerRegistry ──► native lexers  (lexers/*)
              │                        └► RegexLexer    (regex/*, loads rules/*.rules)
              ├── core/Theme       (theme set + per-language override → style table)
              └── view/Highlighter (tokens + theme → RichEdit formatting; full or lazy)
```

### 4.1 Core interfaces (portable)

```cpp
enum class TokenClass : uint8_t { Default, Keyword, Builtin, Constant, Identifier,
    Function, Type, Decorator, Number, String, StringEscape, Comment, Operator,
    Punctuation, Key, Section, Value, Selector, Property, AtRule, Variable,
    Regex, Template, Error, /* … append only */ Count };

struct Token { uint32_t start; uint32_t length; TokenClass cls; };

// Opaque per-line-start lexer state (e.g. "inside triple-quoted string",
// template-literal nesting depth). Must be small and copyable, which enables
// resumable lexing for lazy mode.
struct LexState { uint32_t mode = 0; uint32_t depth = 0; uint64_t extra = 0; };

class ILexer {
public:
    virtual ~ILexer() = default;
    // Lex [text, text+len) starting in `state`; emit tokens in ascending,
    // non-overlapping order; update `state` to the state at the end.
    virtual void lex(std::u16string_view text, LexState& state,
                     std::vector<Token>& out) = 0;
};

struct LexerFactory {
    std::string languageId;           // "json", "css", "python", "js", "ini"
    std::unique_ptr<ILexer> (*create)(const LanguageOptions&);
};
```

- **Gaps:** gaps between tokens are rendered as `Default`.
- **Line resumability:** every lexer must be able to resume at any line start, given the `LexState` recorded at that line start. This requirement makes lazy highlighting possible (§7.2).
- **Registry:** `LexerRegistry` maps `languageId` plus a backend (`native` or `regex`) to a factory. Adding a language means writing a lexer, registering one line, adding the INI extension entry, and adding theme overrides if needed.

### 4.2 Tokenizer backend selection

- `[General] tokenizer = native | regex` sets the global default to `native`.
- An optional per-language override is available, e.g. `[JSON] tokenizer = regex`.
- **Lexer reuse:** a language section may set `lexer=<id>` to reuse a native lexer under another name (e.g. `[JSONC] lexer=json`). A language with a rules file but no native lexer is handled by the regex backend.
- If `regex` is selected but `rules/<lang>.rules` is missing or invalid, the plugin falls back to `native` and shows an info-bar notice.

### 4.3 Config window backbone (not user-reachable in v1)

- `core/Settings`: a typed settings model with `load(path)` and `save(path)`. It round-trips the INI and preserves comments and unknown keys where practical.
- `ui/ConfigDialog`: a dialog class skeleton with resource template placeholders, tab or page structure for General, Languages and Theme, and a `Settings&` binding. It has no functional controls yet.
- Entry point: a hidden INI flag `enable_config_dialog=false`. When true, a context-menu item "Settings…" opens the stub. This lets the dialog be finished later without architectural changes.

---

## 5. Configuration: `tcl-syntaxhl.ini`

The file lives next to the DLL. Keys use `snake_case`. A missing file, section or key falls back to the built-in defaults shown below. In v1 the plugin only reads this file.

```ini
[General]
; native | regex  (regex currently ships only for JSON)
tokenizer=native
; Files larger than this are not fully highlighted. Accepts bytes or K/M suffix.
max_highlight_size=5M
; true  = files over the limit get lazy (visible-area) highlighting
; false = files over the limit are shown as plain text with an info bar notice
use_lazy_highlight=false
; auto = follow TC dark mode; otherwise the name of a folder under themes/
theme=auto
theme_light=light
theme_dark=dark
font_face=Consolas
font_size=10
; reserved for the future config window
enable_config_dialog=false

[Languages]
; LanguageName=ext1,ext2   (no dots; case-insensitive)
; The lowercase LanguageName selects themes/<set>/<name>.theme and rules/<name>.rules
JSON=json
CSS=css
Python=py,pyw
JS=js,mjs,cjs
INI=ini,cfg,inf

[JSON]
; Allow // and /* */ comments (JSONC). When false, comments are marked as Error.
support_jsonc_comments=false

[CSS]
; Enable SCSS/LESS syntax: // comments, $var / @var, nesting, &, #{...} interpolation,
; @mixin/@include. Tip: add scss,less to CSS= in [Languages] when enabling.
support_scss_less=false
```

Only `[JSON]` and `[CSS]` have language options in v1. The section mechanism is generic, so future options are added there.

---

## 6. Themes

### 6.1 Layout

```
themes/
  light/
    default.theme     ; full base palette for all token classes + view colors
    python.theme      ; only overrides (optional)
    css.theme ...
  dark/
    default.theme
    ...
```

The active set comes from `theme`. With `theme=auto`, the plugin uses `theme_dark` when TC reports dark mode (`lcp_darkmode` in ShowFlags or `lc_newparams`) and `theme_light` otherwise.

Style resolution for a language works from most to least specific:
1. `themes/<set>/<lang>.theme`
2. `themes/<set>/default.theme`
3. The built-in fallback palette compiled into the DLL. This guarantees readable output even with an empty `themes/` folder.

### 6.2 File format (INI-style)

```ini
[View]
background=#FFFFFF
foreground=#000000
infobar_background=#FFF4CE
infobar_foreground=#000000

[Tokens]
; class=foreground[,background][,flags]   flags: bold|italic|underline (combinable with +)
keyword=#0000FF,,bold
string=#A31515
string_escape=#FF6A00
comment=#008000,,italic
number=#098658
key=#0451A5
error=#FF0000,#FFE0E0
```

Token class names are the lowercase `TokenClass` names. Unknown names are ignored, and an omitted class inherits from the next level.

v1 ships `light` and `dark` sets, each with `default.theme` and overrides where useful.

---

## 7. Loading and rendering pipeline

1. **Read:** read the file into memory with a single `ReadFile`.
2. **Decode** with `TextDecoder`:
   - A BOM selects UTF-8, UTF-16 LE or UTF-16 BE.
   - With no BOM, a file that validates as UTF-8 is treated as UTF-8.
   - Otherwise the system ANSI code page (`CP_ACP`) is used.
   - Line endings and tabs are kept exactly as in the file. No tab expansion or reformatting happens.
3. **Choose a mode** from the file size (bytes on disk):
   - size ≤ `max_highlight_size`: **full** highlighting (§7.1).
   - Above the limit with `use_lazy_highlight=true`: **lazy** highlighting (§7.2).
   - Above the limit with `use_lazy_highlight=false`: **plain** text, with the info bar showing, for example:
     `Syntax highlighting disabled: file is 12.4 MB, limit is 5 MB (max_highlight_size in tcl-syntaxhl.ini).`
4. **Info bar:** a thin, non-scrolling strip above the RichEdit with a close (×) button. It is hidden unless there is a notice. Notices include: size-limit plain mode, regex rules fallback, and ANSI-fallback decoding (low priority, optional).

### 7.1 Full mode

- Lex the whole text and build RTF (color table from the resolved theme plus `\cfN` runs).
- Stream it into the RichEdit with `EM_STREAMIN`, with redraw suspended.
- Set tab stops to 4 character widths of the current font.

### 7.2 Lazy mode

- Stream the plain text, then lex only the visible line range plus a margin, applying formatting through TOM (`ITextDocument`/`ITextRange`).
- Keep `LexState` checkpoints every N lines (e.g. 256), built incrementally forward. Jumping far ahead lexes only from the nearest checkpoint.
- Re-highlight on scroll, resize, zoom, find and go-to-line, debounced so scrolling stays smooth. Each line is highlighted once.

### 7.3 Display features

| Feature | Behaviour |
|---|---|
| Find | Through `ListSearchText` (§3). |
| Copy / Select all | Ctrl+C / Ctrl+A, and `lc_copy` / `lc_selectall`. Copies plain text. |
| Word wrap | Follows Lister's wrap toggle (`lcp_wraptext`). |
| Zoom | Ctrl+Mouse wheel through `EM_SETZOOM`. It is per window and not persisted. The base size comes from `font_size`. |
| Go to line | Ctrl+G opens a small modal dialog that accepts `line` or `line:col`. It validates the range, then moves the caret and centers the line (in lazy mode, it highlights that area first). |
| Keyboard navigation | Standard RichEdit navigation (arrows, PgUp/PgDn, Ctrl+Home/End). The control is read-only, with no caret blinking issues in Quick View. |

---

## 8. Language lexers (native backend)

The lexers handle basic, robust highlighting. They are not validators, apart from marking obviously invalid tokens as `Error` where it is cheap.

### 8.1 JSON
- Keys (strings followed by `:`) are `Key`; value strings are `String`; escapes are `StringEscape`.
- Numbers (including exponent and negative), `true`/`false`/`null` (`Constant`), and punctuation `{}[]:,`.
- With `support_jsonc_comments=true`, `//` and `/* */` are `Comment`. When false, they are `Error`.
- Unterminated strings run to end of line as `Error`, and the lexer recovers at the next line.

### 8.2 CSS
- Comments `/* */`, selectors (tag, `.class`, `#id`, `:pseudo`, `::pseudo-element`, `[attr]`, combinators), and at-rules (`@media`, `@import`, `@font-face`, …).
- Inside `{}`: properties, values, numbers with units, `#hex` colors, strings, `url(...)`, functions, and `!important`.
- With `support_scss_less=true`:
  - `//` comments.
  - `$var` (SCSS) and `@var` (LESS) as `Variable`.
  - Nested selectors inside blocks, using a lookahead heuristic to tell a property (`name:` … `;`) from a selector (… `{`).
  - `&`, `#{...}` interpolation, and `@mixin`, `@include`, `@extend`, `@if`, `@else`, `@each`, `@function`, `@return`.

### 8.3 Python (3.12 syntax)
- Keywords, including the soft keywords `match`, `case`, `type` in statement position. Builtins and `True`/`False`/`None`.
- Function or class names after `def` and `class`. Decorators (`@name.attr`).
- Strings with any valid prefix combination (`r b u f` and combos), single and triple quoted. Triple-quoted strings span lines through `LexState`.
- f-strings are highlighted as plain strings; their internals are a non-goal.
- Numbers: int, float, hex/oct/bin, underscores, imaginary `j`. Comments `#`. Operators.

### 8.4 JavaScript (ES2023)
- Keywords, literals (`true false null undefined NaN Infinity`), and identifiers. Function names in `function name` and before `(` (optional, cheap).
- Strings `'…'` `"…"`. Template literals `` `…${expr}…` `` with nested-expression tracking through a `LexState` depth.
- Regex literals versus division, decided by a previous-significant-token heuristic. Comments `//` and `/* */`, which span lines.
- Numbers: decimal, hex/oct/bin, separators `_`, BigInt `n`, exponent.

### 8.5 INI
- `[section]` is `Section`; `key` is `Key`; `=`/`:` is `Operator`; the value is `Value`, with quoted parts as `String` and numbers as `Number`.
- Full-line comments starting with `;` or `#` are `Comment`.

---

## 9. Regex backend (`.rules` files)

- **Location:** `rules/<lang>.rules`, INI-style, read with the same parser as the config and themes.
- **Matching:** rules are tried in order at the current position, and the first match wins. A position matching no rule is emitted as `Default`, advancing one character.
- **Engine:** `std::wregex` (ECMAScript). Regexes compile once per load. Performance is secondary for this testing backend.
- **States:** `[Rules]` is the initial state and `[Rules.<name>]` sections define further states. `N.push=<state>` enters a state after a rule matches, `N.pop=true` returns to the previous one, and `default=<class>` colors unmatched characters in a state. States form a stack of up to 8 levels kept in `LexState`, so rule-based lexers are line-resumable as well. A zero-length match counts only when it changes state, and at most 4 times in a row.

```ini
[Meta]
name=JSON (regex)

[Rules]                  ; initial state
10=comment:/\*           ; N=class:regex, tried in ascending N
10.push=block_comment
30=key:"(?:\\.|[^"\\])*"(?=\s*:)
50=number:-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?(?![\w.])

[Rules.block_comment]
default=comment
10=comment:\*/
10.pop=true
```
This syntax is final for v1 and documented in the README; `dist/rules/json.rules` is the reference.

- **Shipped rules:** only `rules/json.rules` in v1. Malformed rules produce an info-bar notice and a native fallback.

---

## 10. Error handling and robustness

- **Never crash TC.** Exceptions must not cross the C ABI. Every export is wrapped, and an internal failure returns `NULL` or `LISTPLUGIN_ERROR`.
- **Missing files:** a missing or invalid INI, theme or rules file falls back to the built-in defaults. A bad color value ignores that entry.
- **Unreadable files:** a file that cannot be read or decoded returns `NULL`, so TC falls back.
- **Pathological input:** very long lines and binary-looking content must not hang the plugin. Lexers are linear-time, and the regex backend caps matches per position.
- **Memory:** releases everything on `ListCloseWindow`, with no leaks across `ListLoadNext`.

---

## 11. Performance targets (reference: mid-range Win10 PC)

| Scenario | Target |
|---|---|
| 100 KB file, full mode, native lexer | open in < 100 ms |
| 5 MB file (default limit), full mode, native lexer | open in < 1.5 s |
| 100 MB file, lazy mode | first screen in < 500 ms; smooth scrolling |
| 100 MB file, plain mode | limited only by RichEdit text load |

---

## 12. Testing

- **Lexer unit tests** (doctest, portable). For each language they cover:
  - Token class assertions on snippets.
  - Edge cases: unterminated strings, nested template literals, triple quotes across lines, JSONC on/off, SCSS on/off.
  - Resumability: lexing line-by-line with carried `LexState` must equal lexing the whole text.
- **Other components:** decoder tests (BOMs, UTF-8 validation, ANSI fallback), config and theme parser tests (defaults, overrides, malformed input), and regex backend tests against the JSON samples.
- **CI:** all of the above runs on both runners (§2.2).

---

## 13. Repository layout

```
CMakeLists.txt
specs.md
README.md
src/
  core/      Config, Settings, TextDecoder, Theme, LexerRegistry, Token types, IniParser
  lexers/    JsonLexer, CssLexer, PythonLexer, JsLexer, IniLexer
  regex/     RegexLexer, RulesParser
  plugin/    exports.cpp, tcl-syntaxhl.def, listplug.h (TC SDK header)
  view/      ListerWindow, InfoBar, Highlighter (RTF builder, TOM lazy painter)
  ui/        GotoLineDialog, ConfigDialog (stub), resources.rc
dist/
  tcl-syntaxhl.ini, pluginst.inf, themes/, rules/
tests/
  doctest.h, test_*.cpp, samples/
.github/workflows/ci.yml
```

---

## 14. Milestones

1. **Skeleton:** CMake, core library, INI parser, config model, CI with both runners green.
2. **Core lexers:** all five native lexers, tests, decoder.
3. **Plugin MVP:**
   - Exports and window with RichEdit.
   - Full-mode RTF highlighting with built-in palette.
   - Detect string from the INI.
4. **Themes:** theme sets, overrides, and dark-mode auto selection.
5. **Viewer features:**
   - Find, copy/select all, wrap, zoom, Ctrl+G, key forwarding, percent reporting, `ListLoadNext`.
6. **Large files:** size limit, info bar, lazy mode with checkpoints.
7. **Regex backend:** `.rules` parser, RegexLexer, `json.rules`, backend switching and fallback.
8. **Backbone and packaging:** Settings round-trip, ConfigDialog stub, release zip, README.

---

## 15. Acceptance criteria (v1)

- [ ] Installing the zip in TC x64 registers the plugin, and F3 on `.json/.css/.py/.js/.ini` shows highlighted text.
- [ ] Unmapped extensions are not claimed by the plugin.
- [ ] UTF-8 (BOM/no BOM), UTF-16 LE/BE (BOM) and ANSI files display correctly.
- [ ] Tabs and line endings are shown as in the file.
- [ ] `theme=auto` follows TC dark mode, and switching `theme` changes the whole look.
- [ ] Per-language `.theme` overrides apply.
- [ ] A file above `max_highlight_size` shows as plain text with the explanatory info bar.
- [ ] With `use_lazy_highlight=true`, the same file opens quickly and highlights while scrolling.
- [ ] `support_jsonc_comments` and `support_scss_less` change highlighting as described.
- [ ] `tokenizer=regex` highlights JSON through `json.rules`. Other languages fall back to native with a notice.
- [ ] F7/Find next, Ctrl+C, Ctrl+A, wrap toggle, Ctrl+wheel zoom and Ctrl+G all work.
- [ ] Lister hotkeys (Esc, N/P, etc.) still work.
- [ ] CI is green on Windows and Linux, and the Windows job produces the release zip.

---

## 16. Implementation notes (v1 as built)

- **Lexer base:** native lexers derive from `LineLexer`, which feeds one line at a time. Anything that crosses a line boundary lives in `LexState`, which guarantees resumability; tests check it for every lexer. Gaps (Default) are not emitted as tokens.
- **Line endings:** text is normalised to `\r` line endings, so lexer offsets equal RichEdit character positions. NUL characters are shown as spaces.
- **Full mode:**
  - Text is streamed as RTF. Tokens that look like plain text produce no run, and same-style tokens separated only by blanks are merged.
  - Wrap and size are applied before streaming, because re-laying out a big formatted document is expensive.
- **Lazy mode:**
  - Plain text is loaded, then visible lines are formatted through TOM (`ITextRange`), which never moves the selection or the scroll position.
  - Painting is triggered by scroll, size and key events, plus a 100 ms timer.
  - Tokens of extremely long lines are applied only near the visible character window and kept pending, capped at 2M.
- **Scrolling:** RichEdit scroll positions are 16-bit and wrap on tall documents. Percent reporting, `lc_setpercent`, go-to-line centering and the restore logic therefore use character positions and line numbers (`EM_CHARFROMPOS`, `EM_LINESCROLL`), never `EM_GETSCROLLPOS`/`EM_SETSCROLLPOS`.
- **Quick View:** detected when the parent window class is not `TLister`; the plugin does not take focus in that case.
- **Encoding notice:** files decoded with the ANSI fallback show no notice. The spec marked it optional, and legacy INI files would trigger it constantly.
- **Tests:** besides the unit tests, `tools/plugin_smoke` drives the real DLL in a fake `TLister` host. It verifies colors against the core lexers, search, commands, `ListLoadNext`, encodings, plain, lazy and regex modes, and language options. CI runs it on `windows-latest`.

---

## 17. Open questions / assumptions to confirm

1. **v1 feature set:** Find, Copy/Select all and word wrap are included; printing is deferred. This was assumed because the question went unanswered.
2. **Tab width:** fixed at 4 columns, since no INI option was requested. An option can be added if needed.
3. **Example plugins and the TC Lister SDK docs:** referenced in the project description but not present in the project. This spec relies on the standard `listplug.h` API and will be cross-checked once the docs are available.
4. **`pluginst.inf`:** the package uses `file=tcl-syntaxhl.wlx` (TC x64 picks the `.wlx64`); this needs verification with a real TC install.
5. **Regex `.rules` syntax and state support:** settled (§9).
6. **INI key naming:** the interview example `support-jsonc_comment` was normalised to `support_jsonc_comments`, using snake_case consistently.
