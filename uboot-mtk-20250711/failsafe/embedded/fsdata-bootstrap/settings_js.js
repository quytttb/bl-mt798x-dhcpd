/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 */

(function () {
    "use strict";

    const SETTINGS_STATUS_ID = "settings_status";
    const ACCENT_HOST_ID = "settings_accent_host";
    const MTD_SECTION_ID = "set_mtd_section";
    const MTD_SELECT_ID = "set_mtd_layout";
    const MTD_CUSTOM_ROW_ID = "set_mtd_custom_row";
    const MTD_CUSTOM_LABEL = "custom";
    const MTD_CUSTOM_ENV = "mtd_layout_custom";

    let lastEnvSnapshot = {};
    let hasMtdCustomLayout = false;

    function setStatus(message) {
        const el = document.getElementById(SETTINGS_STATUS_ID);
        if (el) el.textContent = message || "";
    }

    function parseEnvList(text) {
        const map = Object.create(null);
        if (!text) return map;
        for (const line of text.split("\n")) {
            const idx = line.indexOf("=");
            if (idx <= 0) continue;
            const key = line.substring(0, idx);
            const value = line.substring(idx + 1);
            map[key] = value;
        }
        return map;
    }

    function applyCurrentValues(envMap) {
        for (const node of document.querySelectorAll("[data-env]")) {
            const name = node.getAttribute("data-env");
            if (!name) continue;
            const value = envMap[name];
            node.value = value === undefined ? "" : value;
        }
    }

    function snapshotCurrentValues() {
        const snapshot = Object.create(null);
        for (const node of document.querySelectorAll("[data-env]")) {
            const name = node.getAttribute("data-env");
            if (!name) continue;
            snapshot[name] = (node.value || "").trim();
        }
        return snapshot;
    }

    function syncMtdCustomVisibility() {
        const select = document.getElementById(MTD_SELECT_ID);
        const row = document.getElementById(MTD_CUSTOM_ROW_ID);
        if (!select || !row) return;
        row.style.display = "";
    }

    function bindMtdLayoutChange() {
        const select = document.getElementById(MTD_SELECT_ID);
        if (!select || select.dataset.mtdbound === "1") return;
        select.dataset.mtdbound = "1";
    }

    async function populateMtdLayouts() {
        const select = document.getElementById(MTD_SELECT_ID);
        const section = document.getElementById(MTD_SECTION_ID);
        if (!select) return;
        let text = "";
        try {
            const response = await fetch("/getmtdlayout", { method: "GET", cache: "no-store" });
            if (!response.ok) return;
            text = (await response.text()).trim();
        } catch { return; }

        if (!text || text === "error") return;

        /* Show the MTD section for any valid response */
        if (section) section.style.display = "";

        const parts = text.split(";");
        /* parts[0] = current layout label, parts[1..] = available layouts */
        const previousValue = select.value;
        select.options.length = 0;

        const blank = document.createElement("option");
        blank.value = "";
        blank.textContent = t("settings.value.default", "Default");
        select.appendChild(blank);

        const labels = parts.slice(1).filter((s) => s.length > 0 && s !== MTD_CUSTOM_LABEL);
        if (hasMtdCustomLayout && !labels.includes(MTD_CUSTOM_LABEL))
            labels.push(MTD_CUSTOM_LABEL);

        for (const name of labels) {
            const opt = document.createElement("option");
            opt.value = name;
            opt.textContent = name;
            select.appendChild(opt);
        }

        if (previousValue) select.value = previousValue;
        syncMtdCustomVisibility();
    }

    function buildAccentControls() {
        const host = document.getElementById(ACCENT_HOST_ID);
        if (!host || host.dataset.rendered === "1") return;
        if (typeof appendAccentControls !== "function") return;

        appendAccentControls(host);
        host.dataset.rendered = "1";

        if (typeof applyI18n === "function") applyI18n(host);
    }

    function ipToInt(ip) {
        if (!ip) return null;
        const parts = ip.split(".");
        if (parts.length !== 4) return null;
        let n = 0;
        for (const p of parts) {
            const v = parseInt(p, 10);
            if (!Number.isFinite(v) || v < 0 || v > 255 || String(v) !== p.trim())
                return null;
            n = (n << 8) | v;
        }
        return n >>> 0;
    }

    function intToIp(n) {
        return [(n >>> 24) & 0xff, (n >>> 16) & 0xff,
                (n >>> 8) & 0xff, n & 0xff].join(".");
    }

    function syncServerIp() {
        const ipInput = document.getElementById("set_ipaddr");
        const nmInput = document.getElementById("set_netmask");
        const srvInput = document.getElementById("set_serverip");
        if (!ipInput || !nmInput || !srvInput) return;

        const ip = ipToInt((ipInput.value || "").trim());
        const nm = ipToInt((nmInput.value || "").trim());
        if (ip === null || nm === null) return;

        const net = (ip & nm) >>> 0;
        const hostMax = (~nm) >>> 0;
        if (hostMax < 2) return;

        const ipHost = (ip & ~nm) >>> 0;
        const cur = ipToInt((srvInput.value || "").trim());
        if (cur !== null) {
            const curNet = (cur & nm) >>> 0;
            const curHost = (cur & ~nm) >>> 0;
            if (curNet === net && curHost !== 0 &&
                curHost !== hostMax && curHost !== ipHost)
                return;
        }

        let host = hostMax - 1;
        if (host === ipHost) host = hostMax > 2 ? hostMax - 2 : 1;
        if (host < 1) host = 1;

        srvInput.value = intToIp((net | host) >>> 0);
    }

    function bindNetworkSync() {
        const ipInput = document.getElementById("set_ipaddr");
        const nmInput = document.getElementById("set_netmask");
        const inputs = [ipInput, nmInput];
        for (const el of inputs) {
            if (!el || el.dataset.netbound === "1") continue;
            el.dataset.netbound = "1";
            el.addEventListener("input", syncServerIp);
            el.addEventListener("blur", syncServerIp);
        }
    }

    function bindDarkVariantControl() {
        const select = document.getElementById("settings_dark_variant");
        if (!select || select.dataset.bound === "1") return;
        select.dataset.bound = "1";
        select.addEventListener("change", () => {
            const value = select.value || "";
            if (typeof applyDarkVariant === "function") {
                applyDarkVariant(value, { persistLocal: true });
            }
            if (typeof saveDarkVariant === "function") {
                saveDarkVariant(value);
            }
        });
    }

    async function refresh() {
        setStatus(t("env.status.loading", "Loading..."));
        try {
            const response = await fetch("/env/list", { method: "GET", cache: "no-store" });
            if (!response.ok) {
                setStatus(t("env.status.http", "HTTP error:") + " " + response.status);
                return;
            }
            const text = await response.text();
            const envMap = parseEnvList(text);
            hasMtdCustomLayout = Object.prototype.hasOwnProperty.call(envMap, MTD_CUSTOM_ENV) &&
                (envMap[MTD_CUSTOM_ENV] || "").trim() !== "";
            applyCurrentValues(envMap);
            await populateMtdLayouts();
            lastEnvSnapshot = snapshotCurrentValues();
            syncMtdCustomVisibility();
            setStatus(t("env.status.ready", "Ready."));
        } catch (error) {
            setStatus(t("env.status.error", "Error:") + " " +
                (error && error.message ? error.message : String(error)));
        }
    }

    async function postEnvUnset(name) {
        const fd = new FormData();
        fd.append("name", name);
        const response = await fetch("/env/unset", { method: "POST", body: fd });
        const body = await response.text();
        if (!response.ok)
            throw new Error(name + ": " + (body || response.status));
    }

    async function postEnvSet(name, value) {
        const fd = new FormData();
        fd.append("name", name);
        fd.append("value", value);
        const response = await fetch("/env/set", { method: "POST", body: fd });
        const body = await response.text();
        if (!response.ok)
            throw new Error(name + ": " + (body || response.status));
    }

    async function saveSetting(name, value, previousValue) {
        if (value === previousValue) return false;
        if (value === "")
            await postEnvUnset(name);
        else
            await postEnvSet(name, value);
        return true;
    }

    async function save() {
        const current = snapshotCurrentValues();
        const previous = lastEnvSnapshot || {};
        const keys = Object.keys(current);

        setStatus(t("env.status.saving", "Saving..."));

        let changed = 0;
        let firstError = null;

        for (const key of keys) {
            try {
                if (await saveSetting(key, current[key], previous[key] || ""))
                    changed++;
            } catch (error) {
                if (!firstError) firstError = error;
            }
        }

        if (firstError) {
            setStatus(t("env.status.error", "Error:") + " " +
                (firstError.message ? firstError.message : String(firstError)));
            return;
        }

        if (!changed) {
            setStatus(t("settings.status.nochange", "No changes."));
            return;
        }

        setStatus(t("env.status.saved", "Saved."));
        await refresh();
    }

    function init() {
        buildAccentControls();
        bindDarkVariantControl();
        bindNetworkSync();
        bindMtdLayoutChange();
        refresh();
    }

    /* publish */
    window.settingsInit = init;
    window.settingsRefresh = refresh;
    window.settingsSave = save;
})();
