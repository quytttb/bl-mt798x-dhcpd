/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 */

/**
 * Failsafe Theme Bootstrap
 *
 * Runs synchronously before main.js to apply cached theme preferences,
 * eliminating the flash of unstyled/wrong-theme content (FOUC).
 *
 * Self-contained — does not depend on main.js. main.js will later
 * re-apply and extend these settings via window-level hooks.
 */
(() => {
    "use strict";

    const STORAGE_KEYS = Object.freeze({
        theme: "theme",
        accent: "failsafe_theme_color_cache",
        darkVariant: "failsafe_theme_dark_variant_cache",
    });
    const TRANSITION_DURATION_MS = 620;
    const HEX_SHORT = /^[0-9a-f]{3}$/i;
    const HEX_FULL  = /^[0-9a-f]{6}$/i;
    const ACCENT_PRESETS = ["#2563eb", "#0ea5e9", "#14b8a6", "#10b981", "#f59e0b", "#ef4444", "#ec4899", "#a855f7"];
    const THEME_COLOR_RAINBOW = "rainbow";

    /* ── Preferences ────────────────────────────────────────── */
    let prefersReducedMotion = false;
    let mqReducedMotion = null;
    try {
        mqReducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
        prefersReducedMotion = !!mqReducedMotion?.matches;
    } catch { /* keep default */ }

    /* ── Helpers ─────────────────────────────────────────────── */
    const normalizeHex = (input) => {
        if (input == null) return null;
        let s = String(input).trim();
        if (!s) return null;
        if (s[0] === "#") s = s.slice(1);
        if (!HEX_SHORT.test(s) && !HEX_FULL.test(s)) return null;
        return s.length === 3
            ? `#${s[0]}${s[0]}${s[1]}${s[1]}${s[2]}${s[2]}`.toLowerCase()
            : `#${s.toLowerCase()}`;
    };

    const hexToRgb = (hex) => {
        const n = normalizeHex(hex);
        if (!n) return null;
        return {
            r: parseInt(n.slice(1, 3), 16),
            g: parseInt(n.slice(3, 5), 16),
            b: parseInt(n.slice(5, 7), 16),
        };
    };

    const blendToWhite = (hex, t) => {
        const a = hexToRgb(hex);
        if (!a) return hex;
        const mix = (c) => Math.round(c + (255 - c) * t).toString(16).padStart(2, "0");
        return `#${mix(a.r)}${mix(a.g)}${mix(a.b)}`;
    };

    const readStorage = (key) => {
        try { return localStorage.getItem(key); }
        catch { return null; }
    };

    const applyThemeColorMeta = (color) => {
        if (!color) return;
        let meta = document.querySelector("meta[name='theme-color']");
        if (!meta) {
            meta = document.createElement("meta");
            meta.setAttribute("name", "theme-color");
            document.head?.appendChild(meta);
        }
        meta.setAttribute("content", color);
    };

    const getCachedAccent = () => normalizeHex(readStorage(STORAGE_KEYS.accent));

    const normalizeDarkVariant = (input) => {
        if (input == null) return "";
        const s = String(input).trim().toLowerCase();
        return s === "amoled" ? "amoled" : "";
    };

    /* The chrome bar color the OS reads from <meta name="theme-color">.
     * Picked from accent first, then per-variant fallback. AMOLED gets pure
     * black so the system UI blends with the page edge on OLED screens. */
    const resolveMetaColor = (resolved, variant) => {
        const accent = getCachedAccent();
        if (accent) return accent;
        if (resolved !== "dark") return "#eef2f8";
        return variant === "amoled" ? "#000000" : "#070b16";
    };

    /* ── Theme transition animation ──────────────────────────── */
    /*
     * The class must be present BEFORE the property values change, otherwise
     * the browser jumps to the new values without interpolation. We expose
     * a pulse() function that arms the class, then applyThemeMode() pulses
     * first and uses double-rAF to commit the class before flipping data-theme.
     * The MutationObserver only handles external attribute changes (devtools,
     * legacy callers); the silent flag suppresses double-pulsing on our own
     * changes.
     */
    const setupTransition = (root) => {
        if (prefersReducedMotion) return;

        let timer = null;
        let lastAttr = root.getAttribute("data-theme");

        const pulse = () => {
            if (timer) clearTimeout(timer);
            root.classList.add("theme-transition");
            timer = setTimeout(() => {
                root.classList.remove("theme-transition");
                timer = null;
            }, TRANSITION_DURATION_MS + 60);
        };
        root.__failsafeThemePulse = pulse;

        try {
            new MutationObserver(() => {
                const now = root.getAttribute("data-theme");
                if (now !== lastAttr) {
                    lastAttr = now;
                    if (!root.__failsafeThemeSilent) pulse();
                }
            }).observe(root, { attributes: true, attributeFilter: ["data-theme"] });
        } catch { /* MutationObserver unavailable */ }

        try {
            const mq = window.matchMedia("(prefers-color-scheme: dark)");
            const onChange = () => {
                if (root.getAttribute("data-theme-auto") === "1") {
                    applyThemeMode(root, "auto", false);
                }
            };
            mq.addEventListener?.("change", onChange) ?? mq.addListener?.(onChange);
        } catch { /* matchMedia unavailable */ }
    };

    /* ── Theme mode application ──────────────────────────────── */
    const getPreferredScheme = () => {
        try {
            return window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
        } catch { return "light"; }
    };

    const applyThemeMode = (root, mode, silent) => {
        const schema = (mode === "light" || mode === "dark") ? mode : "auto";
        const isAuto = schema === "auto";
        const resolved = isAuto ? getPreferredScheme() : schema;

        const setAttr = () => {
            if (isAuto) {
                root.setAttribute("data-theme-auto", "1");
            } else {
                root.removeAttribute("data-theme-auto");
            }
            root.setAttribute("data-theme", resolved);
            applyThemeColorMeta(resolveMetaColor(resolved,
                root.getAttribute("data-theme-dark") || ""));
        };

        if (silent || prefersReducedMotion) {
            root.__failsafeThemeSilent = true;
            setAttr();
            /* setTimeout(0) fires after the observer's microtask, so the
             * observer reads silent=true and skips its pulse before we clear it */
            setTimeout(() => { root.__failsafeThemeSilent = false; }, 0);
            return;
        }

        /* arm the transition class first so the property changes interpolate */
        root.__failsafeThemePulse?.();

        /* suppress observer's redundant pulse on our own change */
        root.__failsafeThemeSilent = true;

        /* double rAF: ensures the class addition is committed to the render
         * tree before we mutate the property values it animates */
        requestAnimationFrame(() => requestAnimationFrame(() => {
            setAttr();
            setTimeout(() => { root.__failsafeThemeSilent = false; }, 0);
        }));
    };

    /* ── Dark variant application ────────────────────────────── */
    /*
     * data-theme-dark is set independently of data-theme so the choice
     * persists across auto/light/dark mode flips. CSS only acts on it when
     * data-theme="dark" — see html[data-theme="dark"][data-theme-dark="amoled"]
     * in style.css.
     */
    const applyDarkVariant = (root, variant, silent) => {
        const v = normalizeDarkVariant(variant);

        const setAttr = () => {
            if (v) root.setAttribute("data-theme-dark", v);
            else root.removeAttribute("data-theme-dark");
            applyThemeColorMeta(resolveMetaColor(
                root.getAttribute("data-theme") || "light", v));
        };

        if (silent || prefersReducedMotion) {
            setAttr();
            return;
        }

        /* pulse the same transition class used for mode flips so the palette
         * shift interpolates instead of snapping */
        root.__failsafeThemePulse?.();
        requestAnimationFrame(() => requestAnimationFrame(setAttr));
    };

    /* ── Bootstrap ───────────────────────────────────────────── */
    try {
        const root = document.documentElement;
        const cachedAccent = readStorage(STORAGE_KEYS.accent);
        const cachedTheme  = readStorage(STORAGE_KEYS.theme);
        const cachedDarkVariant = readStorage(STORAGE_KEYS.darkVariant);

        setupTransition(root);

        /* apply dark variant BEFORE mode so resolveMetaColor picks the right
         * fallback on the very first paint */
        applyDarkVariant(root, cachedDarkVariant ?? "", true);

        /* apply cached theme mode (silent — no transition on load) */
        applyThemeMode(root, cachedTheme ?? "auto", true);

        /* expose for main.js */
        window.__failsafeThemeApplyMode = (mode, opts) => {
            applyThemeMode(root, mode, !!opts?.silent);
        };
        window.__failsafeThemeApplyDarkVariant = (variant, opts) => {
            applyDarkVariant(root, variant, !!opts?.silent);
        };

        /* apply cached accent color */
        const resolvedAccent = (cachedAccent === THEME_COLOR_RAINBOW)
            ? ACCENT_PRESETS[Math.floor(Math.random() * ACCENT_PRESETS.length)]
            : cachedAccent;
        const accent = normalizeHex(resolvedAccent);
        const rgb = accent ? hexToRgb(accent) : null;
        if (accent && rgb) {
            root.style.setProperty("--primary", accent);
            root.style.setProperty("--primary-rgb", `${rgb.r}, ${rgb.g}, ${rgb.b}`);
            root.style.setProperty("--primary-2", blendToWhite(accent, 0.28));
            applyThemeColorMeta(accent);
            /* expose resolved accent for main.js so it can update controls */
            window.__failsafeThemeAccent = accent;
        }

        /* re-evaluate reduced motion if OS preference changes post-load */
        mqReducedMotion?.addEventListener?.("change", (e) => {
            prefersReducedMotion = e.matches;
        });
    } catch { /* fail silently — main.js will recover */ }
})();
