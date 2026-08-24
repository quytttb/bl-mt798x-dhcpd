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
    const persistKey = "failsafe_console_output";
    const persistMax = 200000;

    APP_STATE.console = APP_STATE.console || {
        running: false,
        pollTimer: null,
        history: [],
        histPos: -1,
        tokenKey: "failsafe_console_token"
    };

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

    function loadPersistedOutput() {
        if (!outputElement) return;
        try {
            const savedOutput = sessionStorage.getItem(persistKey);
            if (savedOutput) outputElement.textContent = savedOutput;
        } catch (error) { }
    }

    function savePersistedOutput() {
        if (!outputElement) return;
        try {
            let currentOutput = outputElement.textContent || "";
            if (currentOutput.length > persistMax) currentOutput = currentOutput.slice(currentOutput.length - persistMax);
            sessionStorage.setItem(persistKey, currentOutput);
        } catch (error) { }
    }

    function appendText(text) {
        if (!outputElement) return;
        if (!text) return;
        /*
         * Normalize line endings: U-Boot uses \r\n; CSS white-space:pre-wrap
         * honours \r as a real carriage-return, which can overwrite text
         * when \r\n is split across poll boundaries.
         * Convert \r\n→\n first, then any remaining \r→\n.
         */
        text = text.replace(/\r\n/g, "\n").replace(/\r/g, "\n");
        outputElement.textContent += text;
        if (outputElement.textContent.length > persistMax) outputElement.textContent = outputElement.textContent.slice(outputElement.textContent.length - persistMax);
        savePersistedOutput();
        outputElement.scrollTop = outputElement.scrollHeight;
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
        }, 300);
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
            setStatus(t("console.status.running"));
            const response = await fetch("/console/exec", { method: "POST", body: formData });
            const responseText = await response.text();
            if (!response.ok) {
                setStatus(t("console.status.http") + " " + response.status + (responseText ? ": " + responseText : ""));
                return;
            }
            try {
                const payload = JSON.parse(responseText);
                setStatus(t("console.status.ret") + " " + (payload && typeof payload.ret !== "undefined" ? payload.ret : "?"));
            } catch (error) {
                setStatus(t("console.status.done"));
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
                outputElement && (outputElement.textContent = "");
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
