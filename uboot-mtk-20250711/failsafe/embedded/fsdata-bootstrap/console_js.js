/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 */

function consoleInit() {
    const outputElement = document.getElementById("console_out");
    const commandInput = document.getElementById("console_cmd");
    const statusElement = document.getElementById("console_status");
    const tokenInput = document.getElementById("console_token");
    const abortButton = document.getElementById("console_abort");
    const persistKey = "failsafe_console_output";
    const persistMax = 200000;

    APP_STATE.console = APP_STATE.console || {
        running: false,
        pollTimer: null,
        history: [],
        histPos: -1,
        tokenKey: "failsafe_console_token",
        busySince: 0
    };

    /*
     * Repetition period of the poll loop.
     *
     * While a command is executing we poll twice as fast: net commands
     * (tftp, ping, ...) stream their progress through the very same
     * endpoint, and 300 ms makes a '#' progress bar look stuttery.
     */
    function pollDelay() {
        return APP_STATE.console.busySince ? 150 : 300;
    }

    /*
     * Reflect the server-side "busy" flag in the status line.
     *
     * Without this a long-running command looks like a hung page: the
     * fetch for /console/exec stays pending for the whole transfer and
     * nothing on screen changes until it completes.
     */
    function setBusy(busy) {
        if (busy) {
            if (!APP_STATE.console.busySince) APP_STATE.console.busySince = Date.now();
            const seconds = Math.floor((Date.now() - APP_STATE.console.busySince) / 1000);
            abortButton && (abortButton.style.display = "");
            setStatus(t("console.status.running") + " · " + seconds + "s");
            return;
        }
        if (APP_STATE.console.busySince) {
            APP_STATE.console.busySince = 0;
            abortButton && (abortButton.style.display = "none");
            setStatus(t("console.status.done"));
        }
    }

    function loadToken() {
        try {
            const storedToken = localStorage.getItem(APP_STATE.console.tokenKey);
            tokenInput && storedToken && (tokenInput.value = storedToken);
        } catch (error) { }
    }

    function saveToken() {
        try {
            tokenInput && localStorage.setItem(APP_STATE.console.tokenKey, tokenInput.value || "");
        } catch (error) { }
    }

    function setStatus(message) {
        statusElement && (statusElement.textContent = message || "");
    }

    /*
     * Persistent storage strategy: instead of storing the rendered
     * innerHTML (multi-MB, harder to sanitize) we keep the raw inbound
     * bytes as a UTF-16 string, capped at `persistMax` chars.
     *
     *   - `loadPersistedOutput()` re-feeds the stored string through
     *     `ansiTerm.feed()` so a page reload reconstructs the same
     *     final grid state.
     *   - `savePersistedOutput()` is called after every `appendText()`
     *     to keep the persisted buffer in step with what was rendered.
     *
     * On the server side `json_escape()` in failsafe/modules/helpers.c
     * preserves the two control bytes the terminal emulator needs —
     * `0x1b` (ESC, the start of every CSI sequence) and `0x08` (BS, the
     * U-Boot progress-bar backspace) — by emitting them as JSON
     * `\u00XX` escapes.  Without that change every byte < 0x20 was
     * silently dropped to a space and the bootmenu arrived as one
     * unreadable blob.
     */
    let rawBuffer = "";

    function loadPersistedOutput() {
        if (!outputElement) return;
        try {
            const savedOutput = sessionStorage.getItem(persistKey);
            if (savedOutput) {
                rawBuffer = savedOutput;
                ansiTerm.feed(savedOutput);
            }
        } catch (error) { }
    }

    function savePersistedOutput() {
        if (!outputElement) return;
        try {
            let saved = rawBuffer || "";
            if (saved.length > persistMax) saved = saved.slice(saved.length - persistMax);
            sessionStorage.setItem(persistKey, saved);
        } catch (error) { }
    }

    /*
     * Tiny VT100 / ANSI terminal emulator.
     *
     * U-Boot's `bootmenu` draws itself with full CSI sequences — cursor
     * positioning (`[r;cH`), screen / line erasure (`[nJ`, `[nK`), and
     * SGR graphics attributes (`[0;34m`, `[1;39m`, `[7m`, ...).  Dumped
     * verbatim to `textContent` these collapse the menu into one
     * unreadable blob of `ESC[...` characters; the failsafe web
     * console therefore runs a minimal in-memory character grid
     * (rows × cols cells, each tagged with fg / bg / bold / reverse)
     * and renders the affected rows as styled HTML on every batch.
     *
     * Supported sequences (a strict superset of what U-Boot emits):
     *
     *   CSI Ps;Ps H     Cursor Position (row, col are 1-based)
     *   CSI Ps J        Erase in Display (0 / 1 / 2 / 3)
     *   CSI Ps K        Erase in Line   (0 / 1 / 2)
     *   CSI Ps A/B/C/D  Cursor Up / Down / Forward / Back
     *   CSI Ps G        Cursor Horizontal Absolute
     *   CSI Ps;... m    SGR (reset / 1 / 4 / 7 / 22 / 27 / 30-37 /
     *                   39 / 40-47 / 49 / 90-97 / 100-107)
     *   ESC [ ? ... l/h Private modes (cursor visibility etc.) — dropped
     *
     * Control characters honored outside CSI:
     *
     *   \r   carriage return — rewinds cursor to column 0
     *   \n   line feed       — advances to next row (scrolls up at bottom)
     *   \b   backspace       — moves cursor one column backward
     *   \t   tab             — advances to the next 4-column boundary
     *
     *  A CSI sequence split across two poll batches is buffered in
     *  `pending` and resumed on the next batch so a fragmented escape
     *  never silently disappears.
     */
    const ansiTerm = (function () {
        const DEFAULT_FG = "#d6deea";
        const DEFAULT_BG = "#0a0f1a";

        const FG_COLORS = {
            30: "#1a1a1a", 31: "#ef4444", 32: "#22c55e", 33: "#eab308",
            34: "#3b82f6", 35: "#a855f7", 36: "#06b6d4", 37: "#e5e7eb",
            90: "#6b7280", 91: "#f87171", 92: "#4ade80", 93: "#facc15",
            94: "#60a5fa", 95: "#c084fc", 96: "#67e8f9", 97: "#f3f4f6"
        };
        const BG_COLORS = {
            40: "#1a1a1a", 41: "#7f1d1d", 42: "#14532d", 43: "#713f12",
            44: "#1e3a8a", 45: "#581c87", 46: "#164e63", 47: "#e5e7eb",
            100: "#525252", 101: "#b91c1c", 102: "#166534", 103: "#a16207",
            104: "#1d4ed8", 105: "#7e22ce", 106: "#0e7490", 107: "#ffffff"
        };

        const GRID_MAX_ROWS = 200;
        const GRID_MAX_COLS = 160;
        const GRID_INIT_COLS = 80;

        function safeColor(c) {
            /*
             * Defense in depth: every fg / bg we render is taken from
             * FG_COLORS / BG_COLORS, but a malformed SGR could in
             * principle sneak an unrelated string in.  Whitelist the
             * legitimate hex form before injecting into a CSS value;
             * `null` (inherit) and anything unexpected both become
             * null, which the renderer omits from the inline style.
             */
            if (c === null) return null;
            return /^#[0-9a-f]{6}$/i.test(c) ? c : null;
        }

        function makeCell(fg, bg, bold, reverse) {
            return { ch: " ", fg: fg, bg: bg, bold: !!bold, reverse: !!reverse };
        }

        function blankRow(width) {
            /*
             * Blank cells carry `fg: null` / `bg: null` (i.e. "inherit").
             * They must NOT paint an opaque default background, otherwise
             * every row becomes a solid band that covers the console's
             * overall background gradient (see renderRow — a null bg is
             * simply omitted from the inline style).
             */
            const row = new Array(width);
            for (let i = 0; i < width; i++) row[i] = makeCell(null, null, false, false);
            return row;
        }

        const state = {
            grid: [],
            rows: 0,
            cols: 0,
            cursorRow: 0,
            cursorCol: 0,
            /*
             * Grid row where the *current* screen begins.  Content
             * above this row is history that `[2J` preserves (the web
             * console behaves like a terminal with a large scrollback,
             * so a bootmenu that clears the screen on entry/exit does
             * not destroy the banner and menu the user wants to re-read).
             */
            screenTop: 0,
            sgr: { bold: false, fg: null, bg: null, reverse: false },
            rowEls: [],
            dirty: new Set(),
            renderQueued: false
        };

        /* Half-parsed CSI sequence carried across a poll boundary. */
        const pending = { escaping: false, params: "" };

        function resetSgr() {
            state.sgr.bold = false;
            state.sgr.fg = null;
            state.sgr.bg = null;
            state.sgr.reverse = false;
        }

        function markDirty(row) {
            if (row >= 0 && row < state.rows) state.dirty.add(row);
        }

        function ensureSize(targetRow, targetCol) {
            if (state.cols === 0) state.cols = GRID_INIT_COLS;
            if (state.rows < targetRow + 1) {
                for (let r = state.rows; r <= targetRow; r++) state.grid.push(blankRow(state.cols));
                state.rows = targetRow + 1;
            }
            if (state.cols < targetCol + 1) {
                const newCols = Math.min(GRID_MAX_COLS, Math.max(targetCol + 1, Math.max(state.cols * 2, GRID_INIT_COLS)));
                for (let r = 0; r < state.rows; r++) {
                    const oldRow = state.grid[r];
                    const newRow = blankRow(newCols);
                    for (let c = 0; c < oldRow.length; c++) newRow[c] = oldRow[c];
                    state.grid[r] = newRow;
                }
                state.cols = newCols;
            }
        }

        function clampRow(r) { return r < 0 ? 0 : r >= GRID_MAX_ROWS ? GRID_MAX_ROWS - 1 : r; }
        function clampCol(c) { return c < 0 ? 0 : c >= GRID_MAX_COLS ? GRID_MAX_COLS - 1 : c; }

        function applySgr(params) {
            const list = (params && params.length) ? params.split(";") : [];
            if (list.length === 1 && list[0] === "") list[0] = "0";
            if (list.length === 0) list.push("0");
            let i = 0;
            while (i < list.length) {
                const code = parseInt(list[i], 10);
                if (isNaN(code) || code === 0) { resetSgr(); i++; continue; }
                if (code === 1 || code === 4)   { state.sgr.bold = true; i++; continue; }
                if (code === 22 || code === 24) { state.sgr.bold = false; i++; continue; }
                if (code === 7)                 { state.sgr.reverse = true; i++; continue; }
                if (code === 27)                { state.sgr.reverse = false; i++; continue; }
                if (code >= 30 && code <= 37)   { state.sgr.fg = FG_COLORS[code]; i++; continue; }
                if (code >= 90 && code <= 97)   { state.sgr.fg = FG_COLORS[code]; i++; continue; }
                if (code === 39)                { state.sgr.fg = null; i++; continue; }
                if (code >= 40 && code <= 47)   { state.sgr.bg = BG_COLORS[code]; i++; continue; }
                if (code >= 100 && code <= 107) { state.sgr.bg = BG_COLORS[code]; i++; continue; }
                if (code === 49)                { state.sgr.bg = null; i++; continue; }
                i++;
            }
        }

        function scrollUp() {
            if (state.rows === 0) return;
            state.grid.shift();
            state.grid.push(blankRow(state.cols));
            /* Detach the first row div so the new bottom row gets a
             * fresh one; the live DOM tree mirrors the grid indices. */
            const old = state.rowEls.shift();
            if (old && old.parentNode) old.parentNode.removeChild(old);
            if (state.screenTop > 0) state.screenTop--;
            for (let r = 0; r < state.rows; r++) state.dirty.add(r);
        }

        function lineFeed() {
            if (state.cursorRow + 1 >= GRID_MAX_ROWS) {
                scrollUp();
                state.cursorRow = GRID_MAX_ROWS - 1;
            } else {
                state.cursorRow = clampRow(state.cursorRow + 1);
                ensureSize(state.cursorRow, 0);
            }
        }

        function carriageReturn() { state.cursorCol = 0; }
        function backspace() { if (state.cursorCol > 0) state.cursorCol--; }

        function cursorPosition(row, col) {
            /*
             * CUP `[r;cH` positions on the *current screen* (rows 1..N
             * from screenTop down).  Anything above screenTop is
             * history and must never be overwritten by absolute
             * addressing, so map the logical row onto screenTop.
             */
            const targetRow = state.screenTop + (row || 1) - 1;
            ensureSize(targetRow, (col || 1) - 1);
            state.cursorRow = clampRow(targetRow);
            state.cursorCol = clampCol((col || 1) - 1);
        }

        function eraseDisplay(mode) {
            const m = mode === undefined ? 0 : mode;
            if (m === 2 || m === 3) {
                /*
                 * ED(2)/ED(3) — "clear screen".
                 *
                 * Instead of destroying the grid (which made the failsafe
                 * console a big empty area once a bootmenu ran and
                 * cleared itself on exit) we *preserve* everything above
                 * the current screen as history and start a fresh screen
                 * below it.  The document only ever grows, exactly like
                 * a serial terminal with a large scrollback, so the
                 * banner and the bootmenu stay readable after the menu
                 * is gone.
                 *
                 * Do NOT grow the grid here: rows are allocated on
                 * demand when content is actually written, otherwise a
                 * bare `[2J` would leave GRID_INIT_COLS blank rows below
                 * the real content and auto-scroll would show nothing.
                 */
                if (state.cols === 0) state.cols = GRID_INIT_COLS;
                state.screenTop = state.rows;
                state.cursorRow = state.rows;
                state.cursorCol = 0;
            } else if (m === 1) {
                /* Erase from the top of the current screen to the
                 * cursor (inclusive of the cursor cell). */
                const from = Math.max(state.screenTop, 0);
                for (let r = from; r <= state.cursorRow && r < state.rows; r++) {
                    const last = r === state.cursorRow ? state.cursorCol : state.cols - 1;
                    for (let c = 0; c <= last; c++)
                        state.grid[r][c] = makeCell(null, null, false, false);
                    state.dirty.add(r);
                }
            } else {
                /* Erase from the cursor to the end of the current
                 * screen (the whole remaining grid in our model). */
                for (let r = state.cursorRow; r < state.rows; r++) {
                    const first = r === state.cursorRow ? state.cursorCol : 0;
                    for (let c = first; c < state.cols; c++)
                        state.grid[r][c] = makeCell(null, null, false, false);
                    state.dirty.add(r);
                }
            }
        }

        function eraseLine(mode) {
            const m = mode === undefined ? 0 : mode;
            ensureSize(state.cursorRow, 0);
            const r = state.cursorRow;
            if (m === 2) {
                for (let c = 0; c < state.cols; c++)
                    state.grid[r][c] = makeCell(null, null, false, false);
            } else if (m === 1) {
                for (let c = 0; c <= state.cursorCol; c++)
                    state.grid[r][c] = makeCell(null, null, false, false);
            } else {
                for (let c = state.cursorCol; c < state.cols; c++)
                    state.grid[r][c] = makeCell(null, null, false, false);
            }
            state.dirty.add(r);
        }

        function ensureRowEl(row) {
            while (state.rowEls.length <= row) {
                const div = document.createElement("div");
                div.className = "ans-row";
                state.rowEls.push(div);
            }
            return state.rowEls[row];
        }

        function writeChar(ch) {
            if (ch === "\n") {
                /*
                 * LF: advance to the next row AND reset the cursor to
                 * column 0.  Pure VT100 only advances the row, but
                 * U-Boot `printf` emits bare "\n" (no preceding
                 * "\r") and programs assume each line begins at the
                 * left margin.  Without the column reset each line
                 * inherits the previous line's cursor column, so
                 * left-aligned banners like "Telnet server started
                 * on port 23\n" followed by "Web failsafe UI
                 * started\n" produced a stair-stepped block where
                 * every line drifted further right.  CSI H / F
                 * sequences (used by bootmenu) still take precedence
                 * — the explicit positioning path is unaffected.
                 */
                state.cursorCol = 0;
                lineFeed();
                return;
            }
            if (ch === "\r") { carriageReturn(); return; }
            if (ch === "\b") { backspace(); return; }
            /* Drop stray ESC / NUL / BEL — they should always be the
             * first byte of a CSI which the outer parser already
             * consumed; anything left here is noise. */
            if (ch === "\x07" || ch === "\x00" || ch === "\x1b") return;
            if (ch === "\t") {
                ensureSize(state.cursorRow, state.cursorCol);
                for (let i = 0; i < 4; i++) {
                    if (state.cursorCol >= GRID_MAX_COLS) break;
                    const cell = state.grid[state.cursorRow][state.cursorCol];
                    cell.ch = " ";
                    cell.fg = state.sgr.fg;
                    cell.bg = state.sgr.bg;
                    cell.bold = !!state.sgr.bold;
                    cell.reverse = !!state.sgr.reverse;
                    state.cursorCol++;
                }
                state.dirty.add(state.cursorRow);
                return;
            }
            ensureSize(state.cursorRow, state.cursorCol);
            const cell = state.grid[state.cursorRow][state.cursorCol];
            cell.ch = ch;
            cell.fg = state.sgr.fg;
            cell.bg = state.sgr.bg;
            cell.bold = !!state.sgr.bold;
            cell.reverse = !!state.sgr.reverse;
            state.cursorCol++;
            state.dirty.add(state.cursorRow);
            if (state.cursorCol >= GRID_MAX_COLS) {
                state.cursorCol = 0;
                lineFeed();
            }
        }

        function handleCsi(params, final) {
            /* Private-mode prefixes ([?25l cursor visibility etc.) —
             * none affect the visual model; discard. */
            if (params.length && params.charAt(0) === "?") return;
            if (final === "m") { applySgr(params); return; }
            const parts = params.split(";");
            const num = function (idx) {
                const v = parseInt(parts[idx], 10);
                return isNaN(v) ? undefined : v;
            };
            if (final === "H" || final === "f") {
                cursorPosition(num(0), num(1));
            } else if (final === "J") {
                eraseDisplay(num(0));
            } else if (final === "K") {
                eraseLine(num(0));
            } else if (final === "A") {
                /* Cursor up must not climb into the history region. */
                state.cursorRow = Math.max(state.screenTop, state.cursorRow - (num(0) || 1));
            } else if (final === "B") {
                ensureSize(state.cursorRow + (num(0) || 1), 0);
                state.cursorRow = clampRow(state.cursorRow + (num(0) || 1));
            } else if (final === "C") {
                state.cursorCol = clampCol(state.cursorCol + (num(0) || 1));
            } else if (final === "D") {
                state.cursorCol = clampCol(state.cursorCol - (num(0) || 1));
            } else if (final === "G") {
                state.cursorCol = clampCol((num(0) || 1) - 1);
            }
            /* Unknown finals (`n` cursor-position report, ...) silently
             * dropped — the browser side has nothing to reply with. */
        }

        function feed(text) {
            if (!text) return;
            let i = 0;
            const n = text.length;
            while (i < n) {
                if (pending.escaping) {
                    /* Resume a CSI whose final byte arrived in this
                     * batch (or whose terminator still hasn't come). */
                    let j = i;
                    while (j < n) {
                        const cc = text.charCodeAt(j);
                        if (cc >= 0x40 && cc <= 0x7e) break;
                        if (cc === 0x1b) { pending.params = ""; break; }
                        j++;
                    }
                    if (j >= n) {
                        pending.params += text.substring(i);
                        scheduleRender();
                        return;
                    }
                    const final = text[j];
                    /*
                     * pending.params carries any bytes that arrived
                     * before this batch (split-at-the-boundary case);
                     * text.substring(i, j) are the new bytes inside
                     * the current batch.  The combined string is what
                     * handleCsi() parses as the CSI parameters.
                     */
                    handleCsi(pending.params + text.substring(i, j), final);
                    pending.params = "";
                    pending.escaping = false;
                    i = j + 1;
                    continue;
                }
                const c = text.charCodeAt(i);
                if (c === 0x1b) {
                    if (i + 1 < n && text.charCodeAt(i + 1) === 0x5b /* '[' */) {
                        pending.escaping = true;
                        pending.params = "";
                        i += 2;
                    } else {
                        /* Lone ESC or other unsupported escape prefix —
                         * advance past it and keep parsing. */
                        i++;
                    }
                    continue;
                }
                writeChar(text[i]);
                i++;
            }
            scheduleRender();
        }

        function escapeHtml(s) {
            return s.replace(/[&<>"]/g, function (c) {
                return ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;" })[c];
            });
        }

        function renderRow(rowIdx) {
            const row = state.grid[rowIdx];
            if (!row) return "";
            let html = "";
            let i = 0;
            /* Coalesce adjacent same-styled cells into one <span> so
             * a 160-cell row does not become 160 DOM nodes. */
            while (i < row.length) {
                const cell = row[i];
                let j = i + 1;
                while (
                    j < row.length &&
                    row[j].fg === cell.fg &&
                    row[j].bg === cell.bg &&
                    row[j].bold === cell.bold &&
                    row[j].reverse === cell.reverse
                ) j++;
                let text = "";
                for (let k = i; k < j; k++) text += row[k].ch;
                /*
                 * A `null` fg / bg means "inherit from the theme" and
                 * is omitted from the inline style.  Ordinary cells
                 * therefore stay transparent — the console's overall
                 * gradient background remains visible — instead of
                 * every row painting an opaque band.
                 *
                 * `reverse` swaps fg and bg at render time so the
                 * underlying cell data still records the bootmenu's
                 * original intent (`[7m` + a later `[34m` re-colors
                 * just the foreground).  Because a default cell has
                 * both values null (transparent), we first materialize
                 * the theme defaults: without that a bare `[7m` would
                 * swap two transparent values and the highlighted
                 * bootmenu item would be invisible.
                 */
                const fg = safeColor(cell.fg);
                const bg = safeColor(cell.bg);
                let style = "";
                if (cell.reverse) {
                    const origFg = fg !== null ? fg : DEFAULT_FG;
                    const origBg = bg !== null ? bg : DEFAULT_BG;
                    style = "color:" + origBg + ";background:" + origFg + ";";
                } else {
                    if (fg !== null) style += "color:" + fg + ";";
                    if (bg !== null) style += "background:" + bg + ";";
                }
                const cls = cell.bold ? " class=\"ans-bold\"" : "";
                const styleAttr = style ? " style=\"" + style + "\"" : "";
                html +=
                    "<span" + cls + styleAttr + ">" +
                    escapeHtml(text) +
                    "</span>";
                i = j;
            }
            return html;
        }

        function scheduleRender() {
            if (state.renderQueued) return;
            state.renderQueued = true;
            requestAnimationFrame(doRender);
        }

        function doRender() {
            state.renderQueued = false;
            if (!outputElement) return;
            /*
             * Coalesced rAF render: only re-emit rows that changed
             * since the previous frame.  Adjacent `.ans-row` divs are
             * stable so the browser can re-use layout slots.
             *
             * Trailing-blank trim: rows past the last non-blank cell
             * (and past the cursor / screen start) would otherwise add
             * empty scroll space that pushes real content out of the
             * auto-scrolled view — with a cleared bootmenu this turned
             * the console into one large blank area.  We render only
             * up to `keep`; later writes re-extend via ensureRowEl().
             */
            let lastContent = -1;
            for (let r = state.rows - 1; r >= 0; r--) {
                const row = state.grid[r];
                if (!row) continue;
                for (let c = row.length - 1; c >= 0; c--) {
                    if (row[c].ch !== " ") { lastContent = r; break; }
                }
                if (lastContent >= 0) break;
            }
            const keep = state.rows
                ? Math.max(state.screenTop, lastContent + 1, state.cursorRow + 1)
                : 0;

            const dirty = state.dirty;
            for (let r = 0; r < keep; r++) {
                if (!dirty.has(r)) continue;
                const div = ensureRowEl(r);
                div.innerHTML = renderRow(r);
                if (div.parentNode !== outputElement) outputElement.appendChild(div);
            }
            dirty.clear();

            while (state.rowEls.length > keep) {
                const d = state.rowEls.pop();
                if (d.parentNode) d.parentNode.removeChild(d);
            }

            /*
             * Sticky-bottom scroll: only auto-scroll when the user is
             * already at the bottom, so a new chunk of output does not
             * yank them out of the history they are reading.
             */
            const pinned = outputElement.scrollTop + outputElement.clientHeight >=
                outputElement.scrollHeight - 8;
            if (pinned) outputElement.scrollTop = outputElement.scrollHeight;
        }

        function clear() {
            state.grid = [];
            state.rows = 0;
            state.cols = 0;
            state.cursorRow = 0;
            state.cursorCol = 0;
            state.screenTop = 0;
            resetSgr();
            pending.escaping = false;
            pending.params = "";
            state.dirty.clear();
            for (let i = 0; i < state.rowEls.length; i++) {
                const d = state.rowEls[i];
                if (d.parentNode) d.parentNode.removeChild(d);
            }
            state.rowEls = [];
        }

        return { feed: feed, clear: clear, render: doRender, state: state };
    })();

    /*
     * Append console output with terminal semantics.
     *
     * Honors three control character uses:
     *
     *   \r       cursor rewinds to column 0 of the current row
     *   \n       cursor advances to the next row (scrolls up at the bottom)
     *   \r\n     the \n does the work; the \r is handled first (col 0)
     *   \b       cursor backs up one column (overwrite on next write)
     *
     * And once bootmenu (or any other program) starts emitting full
     * CSI sequences the `ansiTerm` module maintains a VT100-style
     * character grid with cursor positioning, line / screen erasure
     * and SGR colors.  Without that interpreter a bootmenu collapses
     * into one unreadable blob of `ESC[...]` text — see the screenshot
     * for the failure mode this replaces.
     */
    function appendText(text) {
        if (!outputElement) return;
        if (!text) return;
        if (rawBuffer.length > persistMax) {
            rawBuffer = rawBuffer.slice(rawBuffer.length - persistMax);
        }
        rawBuffer += text;
        ansiTerm.feed(text);
        savePersistedOutput();
    }

    async function pollOnce() {
        if (!APP_STATE.console.running) return;
        try {
            const formData = new FormData();
            if (tokenInput && tokenInput.value) formData.append("token", tokenInput.value);
            const response = await fetch("/console/poll", { method: "POST", body: formData });
            if (!response.ok) {
                setStatus(t("console.status.http") + " " + response.status);
                return;
            }
            const responseText = await response.text();
            let payload;
            try {
                payload = JSON.parse(responseText);
            } catch (error) {
                setStatus(t("console.status.parse"));
                return;
            }
            payload && payload.data && appendText(payload.data);
            setBusy(!!(payload && payload.busy));
            if (payload && payload.overflow) {
                setStatus(String.fromCodePoint(0x26A0) + " " + t("console.status.overflow"));
            }
        } catch (error) {
            setStatus(t("console.status.error") + " " + (error && error.message ? error.message : String(error)));
        }
    }

    function schedulePoll() {
        if (APP_STATE.console.pollTimer) clearTimeout(APP_STATE.console.pollTimer);
        APP_STATE.console.pollTimer = setTimeout(async () => {
            await pollOnce();
            schedulePoll();
        }, pollDelay());
    }

    window.consoleSend = async function () {
        if (!commandInput || !commandInput.value) return;
        saveToken();
        const commandLine = String(commandInput.value);
        commandInput.value = "";
        APP_STATE.console.history.unshift(commandLine);
        APP_STATE.console.history.length > 50 && (APP_STATE.console.history.length = 50);
        APP_STATE.console.histPos = -1;

        try {
            const formData = new FormData();
            formData.append("cmd", commandLine);
            if (tokenInput && tokenInput.value) formData.append("token", tokenInput.value);
            /*
             * Mark busy before the request goes out: the response only
             * arrives once run_command() has finished, which for a
             * network command can be minutes.  The live output is
             * delivered by the poll loop in the meantime.
             */
            setBusy(true);
            const response = await fetch("/console/exec", { method: "POST", body: formData });
            const responseText = await response.text();
            if (!response.ok) {
                APP_STATE.console.busySince = 0;
                abortButton && (abortButton.style.display = "none");
                setStatus(t("console.status.http") + " " + response.status + (responseText ? ": " + responseText : ""));
                return;
            }
            try {
                const payload = JSON.parse(responseText);
                APP_STATE.console.busySince = 0;
                abortButton && (abortButton.style.display = "none");
                setStatus(t("console.status.ret") + " " + (payload && typeof payload.ret !== "undefined" ? payload.ret : "?"));
            } catch (error) {
                APP_STATE.console.busySince = 0;
                abortButton && (abortButton.style.display = "none");
                setStatus(t("console.status.done"));
            }
        } catch (error) {
            setStatus(t("console.status.error") + " " + (error && error.message ? error.message : String(error)));
        }
    };

    /*
     * Request the server to interrupt the running command.
     *
     * Network commands spend their time in net_loop(); the POST is served
     * from inside that very loop and makes it exit, mirroring Ctrl+C on a
     * serial console.  It is safe to repeat while a command is running.
     */
    window.consoleAbort = async function () {
        if (!APP_STATE.console.busySince) return;
        const formData = new FormData();
        if (tokenInput && tokenInput.value) formData.append("token", tokenInput.value);
        try {
            const response = await fetch("/console/abort", { method: "POST", body: formData });
            const responseText = await response.text();
            if (!response.ok) {
                setStatus(t("console.status.http") + " " + response.status + (responseText ? ": " + responseText : ""));
            }
        } catch (error) {
            setStatus(t("console.status.error") + " " + (error && error.message ? error.message : String(error)));
        }
    };

    window.consoleClear = async function () {
        saveToken();
        try {
            const formData = new FormData();
            if (tokenInput && tokenInput.value) formData.append("token", tokenInput.value);
            const response = await fetch("/console/clear", { method: "POST", body: formData });
            if (response.ok) {
                /* Reset the grid and the persisted raw bytes so the
                 * next poll starts from a blank terminal regardless of
                 * which `[2J` state the previous bootmenu painted. */
                rawBuffer = "";
                ansiTerm.clear();
                try { sessionStorage.removeItem(persistKey); } catch (error) { }
                setStatus(t("console.status.cleared"));
            } else {
                setStatus(t("console.status.http") + " " + response.status);
            }
        } catch (error) {
            setStatus(t("console.status.error") + " " + (error && error.message ? error.message : String(error)));
        }
    };

    if (commandInput) {
        commandInput.addEventListener("keydown", function (event) {
            if (event.ctrlKey && (event.key === "c" || event.key === "C")) {
                /*
                 * While a command is running Ctrl+C aborts it (like a
                 * serial console); otherwise it is the ordinary copy
                 * shortcut and must be left to the browser.
                 */
                if (APP_STATE.console.busySince) {
                    event.preventDefault();
                    window.consoleAbort();
                }
                return;
            }
            if (event.key === "Enter") {
                event.preventDefault();
                window.consoleSend();
                return;
            }
            if (event.key === "ArrowUp") {
                const historyEntries = APP_STATE.console.history;
                if (!historyEntries || !historyEntries.length) return;
                APP_STATE.console.histPos = Math.min(historyEntries.length - 1, APP_STATE.console.histPos + 1);
                commandInput.value = historyEntries[APP_STATE.console.histPos] || "";
                event.preventDefault();
                return;
            }
            if (event.key === "ArrowDown") {
                const historyEntriesDown = APP_STATE.console.history;
                if (!historyEntriesDown || !historyEntriesDown.length) return;
                APP_STATE.console.histPos = Math.max(-1, APP_STATE.console.histPos - 1);
                commandInput.value = APP_STATE.console.histPos >= 0 ? (historyEntriesDown[APP_STATE.console.histPos] || "") : "";
                event.preventDefault();
            }
        });
    }

    APP_STATE.console.running = true;
    loadToken();
    loadPersistedOutput();
    setStatus(t("console.status.ready"));
    schedulePoll();
}
