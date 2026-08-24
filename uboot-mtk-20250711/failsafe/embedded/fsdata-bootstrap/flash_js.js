/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 */

function flashSetStatus(message) {
    let statusElement = document.getElementById("flash_status");
    let txt = document.getElementById("flash_status_text");
    let spin = document.getElementById("flash_spinner");
    const busy = message === t("flash.status.uploading") || message === t("flash.status.restoring");
    if (!statusElement) return;
    statusElement.style.display = message ? "flex" : "none";
    txt && (txt.textContent = message || "");
    spin && (spin.style.display = busy ? "block" : "none");
}

function flashSetProgress(percent) {
    let progressElement = document.getElementById("flash_restore_bar"), boundedPercent;
    if (!progressElement) return;
    if (percent === null || percent === undefined) {
        progressElement.style.display = "none";
        return;
    }
    boundedPercent = Math.max(0, Math.min(100, parseInt(percent || 0)));
    progressElement.style.display = "block";
    progressElement.style.setProperty("--percent", boundedPercent)
}

function flashUpdateRangeHint() {
    let rangeHintElement = document.getElementById("flash_range_hint");
    if (!rangeHintElement) return;
    const startValue = parseUserLen(document.getElementById("flash_start").value);
    const endValue = parseUserLen(document.getElementById("flash_end").value);
    if (startValue === null || endValue === null) {
        rangeHintElement.textContent = t("backup.range.hint");
    } else {
        const rangeSize = endValue >= startValue ? endValue - startValue : 0;
        rangeHintElement.textContent = `Start=${bytesToHuman(startValue)}, End=${bytesToHuman(endValue)}, Size=${bytesToHuman(rangeSize)}`;
    }
}

function flashPadHex(value, width) {
    let hexString = value.toString(16).toUpperCase();
    while (hexString.length < width) hexString = "0" + hexString;
    return hexString
}

/* ── Hex grid state ── */
let flashHexBytes = [];
let flashHexModified = new Set();
let flashSelectedByte = -1;
let flashSelectionStart = -1;   /* anchor of range selection */
let flashSelectionEnd = -1;     /* other end of range selection */
let flashIsDragging = false;
let flashDragAnchor = -1;
const FLASH_MAX_SELECTION = 256; /* max bytes selectable at once */

/* ── Pagination ── */
const FLASH_PAGE_SIZE = 512;     /* bytes per page (32 rows) */
let flashCurrentPage = 0;
let flashReadBase = 0;           /* absolute start address of read range */
let flashTotalPages = 0;

/* Return sorted selection range { start, end, count }, or null if no range */
function flashGetSelectionRange() {
    if (flashSelectionStart < 0 || flashSelectionEnd < 0) return null;
    const lo = Math.min(flashSelectionStart, flashSelectionEnd);
    const hi = Math.max(flashSelectionStart, flashSelectionEnd);
    return { start: lo, end: hi, count: hi - lo + 1 };
}

/* Set both ends of the range, clamping to FLASH_MAX_SELECTION */
function flashSetSelectionRange(anchor, cursor) {
    const maxEnd = anchor + FLASH_MAX_SELECTION - 1;
    const minEnd = anchor - FLASH_MAX_SELECTION + 1;
    const clamped = cursor < anchor ? Math.max(cursor, minEnd) : Math.min(cursor, maxEnd);
    flashSelectionStart = anchor;
    flashSelectionEnd = clamped;
    flashSelectedByte = cursor;
    flashRenderHexGrid();
    const inp = document.getElementById("flash_hex_input");
    if (inp) { inp.value = ""; inp.focus(); }
}

/* Serialize byte array to space-separated hex string */
function flashHexBytesToHexStr() {
    const parts = [];
    for (let i = 0; i < flashHexBytes.length; i++) parts.push(flashPadHex(flashHexBytes[i], 2));
    return parts.join(" ")
}

/* Parse hex string into byte array */
function flashHexStrToBytes(hexStr) {
    const matches = (hexStr || "").match(/[0-9a-fA-F]{2}/g);
    if (!matches) return [];
    const arr = [];
    for (let i = 0; i < matches.length; i++) arr.push(parseInt(matches[i], 16));
    return arr
}

/* Ensure there is always a selected byte (first byte if nothing selected) */
function flashEnsureHexSelected() {
    if (flashHexBytes.length === 0) return false;
    if (flashSelectedByte < 0 || flashSelectedByte >= flashHexBytes.length)
        flashSelectedByte = 0;
    return true
}

/* Render the hex byte grid — only current page */
function flashRenderHexGrid() {
    const grid = document.getElementById("flash_hex_grid");
    if (!grid) return;
    const range = flashGetSelectionRange();
    const pageStart = flashCurrentPage * FLASH_PAGE_SIZE;
    const pageEnd = Math.min(pageStart + FLASH_PAGE_SIZE, flashHexBytes.length);
    grid.innerHTML = "";
    for (let row = pageStart; row < pageEnd; row += 16) {
        const rowDiv = document.createElement("div");
        rowDiv.className = "hex-row";
        for (let col = 0; col < 16 && row + col < pageEnd; col++) {
            const idx = row + col;
            const span = document.createElement("span");
            span.className = "hex-byte";
            span.dataset.index = idx;
            span.textContent = flashPadHex(flashHexBytes[idx], 2);
            const inRange = range && idx >= range.start && idx <= range.end;
            if (inRange) span.classList.add("in-range");
            if (idx === flashSelectedByte) span.classList.add("selected");
            if (flashHexModified.has(idx)) span.classList.add("modified");
            rowDiv.appendChild(span);
        }
        grid.appendChild(rowDiv);
    }
}

/* Render offset and ASCII columns — current page only, absolute addresses */
function flashRenderHexViews() {
    const offsetEl = document.getElementById("flash_offset");
    const asciiEl = document.getElementById("flash_ascii");
    if (!offsetEl || !asciiEl) return;
    const pageStart = flashCurrentPage * FLASH_PAGE_SIZE;
    const pageEnd = Math.min(pageStart + FLASH_PAGE_SIZE, flashHexBytes.length);
    const offLines = [];
    const asciiLines = [];
    for (let row = pageStart; row < pageEnd; row += 16) {
        offLines.push("0x" + flashPadHex(flashReadBase + row, 8));
        for (let col = 0; col < 16 && row + col < pageEnd; col++) {
            const b = flashHexBytes[row + col];
            asciiLines.push(b >= 0x20 && b <= 0x7E ? String.fromCharCode(b) : ".");
        }
        if (row + 16 > pageEnd)
            for (let col = pageEnd - row; col < 16; col++) asciiLines.push(" ");
        asciiLines.push("\n");
    }
    offsetEl.textContent = offLines.join("\n");
    asciiEl.textContent = asciiLines.join("").replace(/\n$/, "");
}

/* Select a byte; auto-navigate page if needed */
function flashSelectByte(index) {
    if (index < 0 || index >= flashHexBytes.length) return;
    const newPage = Math.floor(index / FLASH_PAGE_SIZE);
    if (newPage !== flashCurrentPage) {
        flashCurrentPage = newPage;
        flashUpdatePageControls();
    }
    flashSelectedByte = index;
    flashSelectionStart = index;
    flashSelectionEnd = index;
    if (document.getElementById("flash_hex_input")) document.getElementById("flash_hex_input").value = "";
    flashRenderHexGrid();
    flashRenderHexViews();
    const inp = document.getElementById("flash_hex_input");
    if (inp) inp.focus();
    flashScrollToByte(index);
}

/* Focus the hidden input (for keyboard capture) */
function flashFocusHexInput() {
    const inp = document.getElementById("flash_hex_input");
    if (inp) inp.focus();
}

/* Scroll grid to show the row containing byteIndex (page-relative) */
function flashScrollToByte(byteIndex) {
    const grid = document.getElementById("flash_hex_grid");
    if (!grid || flashHexBytes.length === 0) return;
    const pageStart = flashCurrentPage * FLASH_PAGE_SIZE;
    const rowIdx = Math.floor((byteIndex - pageStart) / 16);
    const rows = grid.querySelectorAll(".hex-row");
    if (rows.length > rowIdx && rowIdx >= 0) {
        const row = rows[rowIdx];
        const gridTop = grid.getBoundingClientRect().top;
        const rowTop = row.getBoundingClientRect().top;
        const rowBottom = row.getBoundingClientRect().bottom;
        if (rowTop < gridTop || rowBottom > gridTop + grid.clientHeight)
            row.scrollIntoView({ block: "nearest" });
    }
    flashSyncScroll();
}

/* Sync scroll among grid, offset and ascii columns */
function flashSyncScroll() {
    const grid = document.getElementById("flash_hex_grid");
    const offsetEl = document.getElementById("flash_offset");
    const asciiEl = document.getElementById("flash_ascii");
    if (!grid || !offsetEl || !asciiEl) return;
    offsetEl.scrollTop = grid.scrollTop;
    asciiEl.scrollTop = grid.scrollTop;
}

/* Jump to an absolute offset (relative to read base) */
function flashJumpToOffset() {
    const jumpInput = document.getElementById("flash_jump");
    const grid = document.getElementById("flash_hex_grid");
    if (!jumpInput || !grid || flashHexBytes.length === 0) return;
    const targetOffset = parseUserLen(jumpInput.value);
    if (targetOffset === null) {
        flashSetStatus(t("flash.error.jump"));
        return;
    }
    const byteIndex = targetOffset - flashReadBase;
    if (byteIndex < 0 || byteIndex >= flashHexBytes.length) {
        flashSetStatus(t("flash.error.jump"));
        return;
    }
    flashSelectByte(byteIndex);
    flashSetStatus("");
}

/* ── Pagination controls ── */
function flashGoToPage(n) {
    n = Math.max(0, Math.min(n, flashTotalPages - 1));
    if (n === flashCurrentPage || !isFinite(n)) return;
    flashCurrentPage = n;
    flashRenderHexGrid();
    flashRenderHexViews();
    flashUpdatePageControls();
    const grid = document.getElementById("flash_hex_grid");
    if (grid) grid.scrollTop = 0;
    flashSyncScroll();
}

function flashUpdatePageControls() {
    const container = document.getElementById("flash_page_controls");
    const info = document.getElementById("flash_page_info");
    if (!container || !info) return;
    const show = flashTotalPages > 1;
    container.style.display = show ? "flex" : "none";
    if (show) {
        info.textContent = (flashCurrentPage + 1) + " / " + flashTotalPages;
        document.getElementById("flash_page_prev").disabled = flashCurrentPage <= 0;
        document.getElementById("flash_page_next").disabled = flashCurrentPage >= flashTotalPages - 1;
    }
}

/* ── Smart write: build contiguous modified-region chunks ── */
function flashBuildWriteChunks() {
    if (flashHexModified.size === 0) return [];
    const sorted = Array.from(flashHexModified).sort(function (a, b) { return a - b; });
    const chunks = [];
    let chunkStart = sorted[0];
    let chunkEnd = sorted[0];
    for (let i = 1; i < sorted.length; i++) {
        if (sorted[i] === chunkEnd + 1) {
            chunkEnd = sorted[i];
        } else {
            chunks.push({ start: chunkStart, end: chunkEnd, count: chunkEnd - chunkStart + 1 });
            chunkStart = sorted[i];
            chunkEnd = sorted[i];
        }
    }
    chunks.push({ start: chunkStart, end: chunkEnd, count: chunkEnd - chunkStart + 1 });
    return chunks;
}

/* ── Keyboard navigation ── */
function flashHandleHexKey(e) {
    if (flashHexBytes.length === 0) return;
    flashEnsureHexSelected();
    const key = e.key;
    const prev = flashSelectedByte;
    let consumed = true;
    if (key === "ArrowRight")       flashSelectedByte = Math.min(flashSelectedByte + 1, flashHexBytes.length - 1);
    else if (key === "ArrowLeft")   flashSelectedByte = Math.max(flashSelectedByte - 1, 0);
    else if (key === "ArrowDown")   flashSelectedByte = Math.min(flashSelectedByte + 16, flashHexBytes.length - 1);
    else if (key === "ArrowUp")     flashSelectedByte = Math.max(flashSelectedByte - 16, 0);
    else if (key === "Tab")         flashSelectedByte = e.shiftKey ? Math.max(flashSelectedByte - 1, 0) : Math.min(flashSelectedByte + 1, flashHexBytes.length - 1);
    else if (key === "Home")        flashSelectedByte = Math.floor(flashSelectedByte / 16) * 16;
    else if (key === "End")         flashSelectedByte = Math.min(Math.floor(flashSelectedByte / 16) * 16 + 15, flashHexBytes.length - 1);
    else if (key === "PageDown")    flashSelectedByte = Math.min(flashSelectedByte + 64, flashHexBytes.length - 1);
    else if (key === "PageUp")      flashSelectedByte = Math.max(flashSelectedByte - 64, 0);
    else if (key === "Escape")      { consumed = false; document.getElementById("flash_hex_input").blur(); }
    else consumed = false;

    if (consumed && flashSelectedByte !== prev) {
        e.preventDefault();
        const inp = document.getElementById("flash_hex_input");
        if (inp) inp.value = "";
        if (e.shiftKey) {
            if (flashSelectionStart < 0) flashSelectionStart = prev;
            flashSelectionEnd = flashSelectedByte;
        } else {
            flashSelectionStart = flashSelectedByte;
            flashSelectionEnd = flashSelectedByte;
        }
        /* auto-navigate page */
        const newPage = Math.floor(flashSelectedByte / FLASH_PAGE_SIZE);
        if (newPage !== flashCurrentPage) {
            flashCurrentPage = newPage;
            flashUpdatePageControls();
        }
        flashRenderHexGrid();
        flashRenderHexViews();
        flashFocusHexInput();
        flashScrollToByte(flashSelectedByte);
    }
}

/* ── Hex input: single byte or fill selected range ── */
function flashHandleHexInput(e) {
    if (flashHexBytes.length === 0) return;
    flashEnsureHexSelected();
    const inp = document.getElementById("flash_hex_input");
    if (!inp) return;
    let val = inp.value.replace(/[^0-9a-fA-F]/g, "").toUpperCase();
    if (val.length > 2) val = val.slice(0, 2);
    inp.value = val;

    if (val.length === 2) {
        const byteVal = parseInt(val, 16);
        if (!isNaN(byteVal)) {
            const range = flashGetSelectionRange();
            if (range && range.count > 1) {
                for (let i = range.start; i <= range.end; i++) {
                    flashHexBytes[i] = byteVal;
                    flashHexModified.add(i);
                }
                flashSetStatus("Filled " + range.count + " bytes");
            } else {
                flashHexBytes[flashSelectedByte] = byteVal;
                flashHexModified.add(flashSelectedByte);
            }
        }
        inp.value = "";
        // auto-advance (skip over range if multi-selected)
        if (flashSelectedByte + 1 < flashHexBytes.length) {
            flashSelectedByte++;
            flashSelectByte(flashSelectedByte);
        } else {
            flashRenderHexGrid();
            flashRenderHexViews();
            flashFocusHexInput();
        }
    }
}

/* ── Mouse drag selection ── */
function flashHexByteAtPoint(clientX, clientY) {
    const elem = document.elementFromPoint(clientX, clientY);
    if (!elem) return -1;
    const target = elem.closest(".hex-byte");
    if (!target) return -1;
    return parseInt(target.dataset.index, 10);
}

function flashHexGridMouseDown(e) {
    const idx = flashHexByteAtPoint(e.clientX, e.clientY);
    if (idx < 0 || idx >= flashHexBytes.length) return;
    e.preventDefault();
    flashIsDragging = true;
    flashDragAnchor = idx;
    flashSetSelectionRange(idx, idx);
    document.addEventListener("mousemove", flashHexGridMouseMove);
    document.addEventListener("mouseup", flashHexGridMouseUp);
}

function flashHexGridMouseMove(e) {
    if (!flashIsDragging) return;
    const idx = flashHexByteAtPoint(e.clientX, e.clientY);
    if (idx < 0 || idx >= flashHexBytes.length) return;
    flashSetSelectionRange(flashDragAnchor, idx);
}

function flashHexGridMouseUp(e) {
    flashIsDragging = false;
    document.removeEventListener("mousemove", flashHexGridMouseMove);
    document.removeEventListener("mouseup", flashHexGridMouseUp);
    flashFocusHexInput();
}

/* Handle click on the hex grid (single click = single select, drag = range) */
function flashHexGridClick(e) {
    if (flashIsDragging) return;
    const idx = flashHexByteAtPoint(e.clientX, e.clientY);
    if (idx >= 0 && idx < flashHexBytes.length) {
        flashSelectByte(idx);
    }
}

function flashFindLastBefore(str, sub, limit) {
    let idx = -1;
    let cur = str.indexOf(sub);
    while (cur !== -1 && cur < limit) {
        idx = cur;
        cur = str.indexOf(sub, cur + 1)
    }
    return idx
}

function flashParseBackupFilename(name) {
    if (!name) return null;
    const rangeIdx = name.indexOf("_0x");
    let dashIdx, startStr, endStr, start, end;
    if (rangeIdx < 0) return null;
    dashIdx = name.indexOf("-0x", rangeIdx);
    if (dashIdx < 0) return null;
    startStr = name.slice(rangeIdx + 1, dashIdx);
    endStr = name.slice(dashIdx + 1);
    start = /^0x[0-9a-fA-F]+/.exec(startStr);
    end = /^0x[0-9a-fA-F]+/.exec(endStr);
    if (!start || !end) return null;
    start = parseInt(start[0], 16);
    end = parseInt(end[0], 16);
    if (!isFinite(start) || !isFinite(end) || end <= start) return null;
    const mtdIdx = flashFindLastBefore(name, "_mtd_", rangeIdx);
    const mmcIdx = flashFindLastBefore(name, "_mmc_", rangeIdx);
    const stypeIdx = mtdIdx >= 0 && mmcIdx >= 0 ? (mtdIdx > mmcIdx ? mtdIdx : mmcIdx) : (mtdIdx >= 0 ? mtdIdx : mmcIdx);
    if (stypeIdx < 0) return null;
    const storage = stypeIdx === mtdIdx ? "mtd" : "mmc";
    const seg = name.slice(stypeIdx + 5, rangeIdx);
    if (!seg) return null;
    const parts = seg.split("_");
    const target = parts[parts.length - 1];
    if (!target) return null;
    return { storage: storage, target: target, start: start, end: end }
}

function flashSelectTarget(val) {
    const targetSelect = document.getElementById("flash_target");
    if (!targetSelect) return false;
    for (let optionIndex = 0; optionIndex < targetSelect.options.length; optionIndex++) {
        if (targetSelect.options[optionIndex].value === val) {
            targetSelect.selectedIndex = optionIndex;
            return true;
        }
    }
    return false
}

function flashGetDeviceNameByStorage(storage) {
    const backupInfo = APP_STATE && APP_STATE.backupinfo ? APP_STATE.backupinfo : null;
    let mmcName = "";
    let mtdName = "";
    if (backupInfo && backupInfo.mmc && backupInfo.mmc.present) {
        mmcName = [backupInfo.mmc.vendor || "", backupInfo.mmc.product || ""].join(" ").trim();
        if (!mmcName) mmcName = "MMC";
    }
    if (backupInfo && backupInfo.mtd && backupInfo.mtd.present) {
        mtdName = (backupInfo.mtd.model || "").trim();
        if (!mtdName) mtdName = "MTD";
    }
    if (storage === "mtd") return mtdName || "MTD";
    if (storage === "mmc") return mmcName || "MMC";
    return mtdName || mmcName || "device";
}

function flashRefreshI18n() {
    const targetSelect = document.getElementById("flash_target");
    if (!targetSelect) return;

    for (let optionIndex = 0; optionIndex < targetSelect.options.length; optionIndex++) {
        const optionElement = targetSelect.options[optionIndex];
        if (optionElement && optionElement.dataset && optionElement.dataset.i18nKey) {
            optionElement.textContent = window.t(optionElement.dataset.i18nKey);
        }
    }

    for (let optionIndex = 0; optionIndex < targetSelect.options.length; optionIndex++) {
        const optionElement = targetSelect.options[optionIndex];
        if (!optionElement || !optionElement.dataset) continue;
        if (optionElement.dataset.kind === "mtd-full") {
            const mtdName = optionElement.dataset.mtdName || "";
            optionElement.textContent = `[MTD] ${window.t("backup.target.full_disk")}${mtdName ? ` (${mtdName})` : ""}${optionElement.dataset.size ? ` (${bytesToHuman(parseInt(optionElement.dataset.size, 10))})` : ""}`;
        }
    }
}

function flashBuildErasePlan() {
    const targetSelect = document.getElementById("flash_target");
    const startInput = document.getElementById("flash_start");
    const endInput = document.getElementById("flash_end");
    let targetValue, targetParts, storageType, targetName, isRawTarget;
    let startText, endText, hasStartRange, hasEndRange, startValue, endValue;
    let targetLabel, detailText;

    if (!targetSelect || !targetSelect.value)
        return { error: t("flash.error.no_target") };

    targetValue = String(targetSelect.value);
    targetParts = targetValue.split(":");
    storageType = targetParts.length > 1 ? targetParts[0] : "auto";
    targetName = targetParts.length > 1 ? targetParts.slice(1).join(":") : targetValue;
    isRawTarget = targetName === "raw";

    startText = startInput && startInput.value ? String(startInput.value).trim() : "";
    endText = endInput && endInput.value ? String(endInput.value).trim() : "";
    hasStartRange = !!startText;
    hasEndRange = !!endText;

    if (hasStartRange !== hasEndRange)
        return { error: t("flash.error.bad_range") };

    if (hasStartRange && hasEndRange) {
        startValue = parseUserLen(startText);
        endValue = parseUserLen(endText);
        if (startValue === null || endValue === null || endValue <= startValue)
            return { error: t("flash.error.bad_range") };
    }

    if (isRawTarget && !hasStartRange)
        return { error: t("flash.error.bad_range") + " (raw target requires start/end)" };

    targetLabel = isRawTarget ? "" : `${targetName} 分区`;
    detailText = hasStartRange ? (isRawTarget ? (`0x${startValue.toString(16)}~0x${endValue.toString(16)}`) : (`${targetLabel} 的 0x${startValue.toString(16)}~0x${endValue.toString(16)}`)) : targetLabel;

    return {
        storage: storageType,
        target: targetValue,
        hasRange: hasStartRange,
        start: hasStartRange ? startValue : null,
        end: hasStartRange ? endValue : null,
        detail: detailText,
        deviceName: flashGetDeviceNameByStorage(storageType)
    };
}

function flashInit() {
    const targetSelect = document.getElementById("flash_target");
    const startInput = document.getElementById("flash_start");
    const endInput = document.getElementById("flash_end");
    const gridElement = document.getElementById("flash_hex_grid");
    const hexInput = document.getElementById("flash_hex_input");
    const infoElement = document.getElementById("flash_info");
    const restoreInfoElement = document.getElementById("flash_restore_info");
    const backupInput = document.getElementById("flash_backup");

    if (startInput) startInput.oninput = () => { flashUpdateRangeHint(); flashRenderHexViews(); };
    if (endInput) endInput.oninput = flashUpdateRangeHint;
    flashUpdateRangeHint();
    flashRenderHexViews();
    flashUpdatePageControls();
    flashSetStatus("");

    if (gridElement) {
        gridElement.addEventListener("mousedown", flashHexGridMouseDown);
        gridElement.addEventListener("click", flashHexGridClick);
        gridElement.addEventListener("scroll", flashSyncScroll);
    }

    if (hexInput) {
        hexInput.addEventListener("keydown", flashHandleHexKey);
        hexInput.addEventListener("input", flashHandleHexInput);
    }

    if (backupInput) backupInput.onchange = () => {
        const selectedFile = backupInput.files && backupInput.files.length ? backupInput.files[0] : null;
        const parsedBackup = selectedFile ? flashParseBackupFilename(selectedFile.name) : null;
        if (!parsedBackup) {
            restoreInfoElement && (restoreInfoElement.textContent = t("flash.detected.none") +
                (selectedFile ? ` (${bytesToHuman(selectedFile.size)})` : ""));
            return;
        }
        restoreInfoElement && (restoreInfoElement.textContent = `${parsedBackup.storage}:${parsedBackup.target} 0x${parsedBackup.start.toString(16)}-0x${parsedBackup.end.toString(16)} (${bytesToHuman(selectedFile.size)})`);
        flashSelectTarget(`${parsedBackup.storage}:${parsedBackup.target}`);
        if (startInput) startInput.value = `0x${parsedBackup.start.toString(16)}`;
        if (endInput) endInput.value = `0x${parsedBackup.end.toString(16)}`;
        flashUpdateRangeHint();
        flashRenderHexViews();
    };

    ajax({
        url: "/backup/info",
        done: function (responseText) {
            let backupInfo;
            try {
                backupInfo = JSON.parse(responseText);
            } catch (error) {
                flashSetStatus("backupinfo parse failed");
                return;
            }

            if (infoElement) {
                const infoParts = [];
                backupInfo.mmc && backupInfo.mmc.present ? infoParts.push(`MMC: ${backupInfo.mmc.vendor || ""} ${backupInfo.mmc.product || ""}`) : infoParts.push("MMC: " + t("backup.storage.not_present"));
                backupInfo.mtd && backupInfo.mtd.present ? infoParts.push(`MTD: ${backupInfo.mtd.model || ""}`) : infoParts.push("MTD: " + t("backup.storage.not_present"));
                infoElement.textContent = infoParts.join(" | ");
            }

            if (!targetSelect) return;
            targetSelect.options.length = 0;
            const placeholderOption = document.createElement("option");
            placeholderOption.value = "";
            placeholderOption.dataset.i18nKey = "backup.target.placeholder";
            targetSelect.appendChild(placeholderOption);

            if (backupInfo.mmc && backupInfo.mmc.present) {
                const rawOption = document.createElement("option");
                rawOption.value = "mmc:raw";
                rawOption.textContent = "[MMC] raw";
                rawOption.dataset.kind = "mmc-raw";
                targetSelect.appendChild(rawOption);
                if (backupInfo.mmc.parts && backupInfo.mmc.parts.length) {
                    backupInfo.mmc.parts.forEach(function (partition) {
                        if (!partition || !partition.name) return;
                        const partitionOption = document.createElement("option");
                        partitionOption.value = "mmc:" + partition.name;
                        partitionOption.textContent = "[MMC] " + partition.name + (partition.size ? " (" + bytesToHuman(partition.size) + ")" : "");
                        partitionOption.dataset.kind = "mmc-part";
                        targetSelect.appendChild(partitionOption);
                    });
                }
            }

            if (backupInfo.mtd && backupInfo.mtd.present && backupInfo.mtd.parts && backupInfo.mtd.parts.length) {
                const mtdType = backupInfo.mtd.type;
                const hasMasterPartitions = mtdType === 3 || mtdType === 4 || mtdType === 8;
                const masterPartitions = [];
                if (hasMasterPartitions) {
                    backupInfo.mtd.parts.forEach(function (partition) {
                        if (partition && partition.name && partition.master) masterPartitions.push(partition);
                    });
                }

                if (hasMasterPartitions && masterPartitions.length) {
                    masterPartitions.forEach(function (partition) {
                        const fullDiskOption = document.createElement("option");
                        fullDiskOption.value = "mtd:" + partition.name;
                        fullDiskOption.dataset.mtdName = partition.name;
                        fullDiskOption.dataset.size = partition.size ? String(partition.size) : "";
                        fullDiskOption.dataset.kind = "mtd-full";
                        targetSelect.appendChild(fullDiskOption);
                    });
                }

                backupInfo.mtd.parts.forEach(function (partition) {
                    if (!partition || !partition.name) return;
                    if (hasMasterPartitions && partition.master) return;
                    const partitionOption = document.createElement("option");
                    partitionOption.value = "mtd:" + partition.name;
                    partitionOption.textContent = "[MTD] " + partition.name + (partition.size ? " (" + bytesToHuman(partition.size) + ")" : "");
                    partitionOption.dataset.kind = "mtd-part";
                    targetSelect.appendChild(partitionOption);
                });
            }

            if (targetSelect.options.length > 1) targetSelect.selectedIndex = 1;
            flashRefreshI18n();
        }
    });
}

const FLASH_READ_CHUNK_SIZE = 256 * 1024; /* must match backend FLASH_READ_CHUNK */

async function flashRead() {
    const targetSelect = document.getElementById("flash_target");
    const startInput = document.getElementById("flash_start");
    const endInput = document.getElementById("flash_end");
    if (!targetSelect || !startInput || !endInput) return;
    if (!targetSelect.value) {
        alert(t("flash.error.no_target"));
        return;
    }
    if (!startInput.value || !endInput.value) {
        alert(t("flash.error.bad_range"));
        return;
    }
    try {
        const totalSize = parseUserLen(endInput.value) - parseUserLen(startInput.value);
        if (totalSize <= 0) {
            alert(t("flash.error.bad_range"));
            return;
        }
        const totalChunks = Math.ceil(totalSize / FLASH_READ_CHUNK_SIZE);
        flashReadBase = parseUserLen(startInput.value);
        flashHexBytes = new Array(totalSize).fill(0);
        flashHexModified = new Set();
        flashTotalPages = Math.ceil(totalSize / FLASH_PAGE_SIZE);
        flashCurrentPage = 0;

        /* optional: warn for large reads */
        if (totalSize > 1024 * 1024 && !confirm(t("flash.confirm.chunk").replace("$1", totalChunks).replace("$2", bytesToHuman(totalSize))))
            return;

        for (var c = 0; c < totalChunks; c++) {
            flashSetStatus(t("flash.status.reading") + " (" + (c + 1) + "/" + totalChunks + ")");
            const formData = new FormData();
            formData.append("op", "read");
            formData.append("storage", "auto");
            formData.append("target", targetSelect.value);
            formData.append("start", startInput.value);
            formData.append("end", endInput.value);
            formData.append("chunk", String(c));
            const response = await fetch("/flash/read", { method: "POST", body: formData });
            const responseText = await response.text();
            if (!response.ok) {
                flashSetStatus(t("flash.status.http") + " " + response.status + (responseText ? ": " + responseText : ""));
                return;
            }
            var payload;
            try { payload = JSON.parse(responseText); } catch (error) {
                flashSetStatus(t("flash.status.error") + " parse");
                return;
            }
            if (!payload || !payload.ok) {
                flashSetStatus(t("flash.status.error") + " " + (payload && payload.error ? payload.error : ""));
                return;
            }
            /* copy chunk data into full byte array */
            var chunkBytes = flashHexStrToBytes(payload.data || "");
            var chunkOffset = c * FLASH_READ_CHUNK_SIZE;
            for (var i = 0; i < chunkBytes.length; i++)
                flashHexBytes[chunkOffset + i] = chunkBytes[i];
            /* render first page immediately so user sees something */
            if (c === 0) {
                flashSelectedByte = flashHexBytes.length > 0 ? 0 : -1;
                flashSelectionStart = flashSelectedByte;
                flashSelectionEnd = flashSelectedByte;
                flashRenderHexGrid();
                flashRenderHexViews();
            }
        }
        flashUpdatePageControls();
        const pageCtl = document.getElementById("flash_page_controls");
        if (pageCtl) pageCtl.style.display = flashTotalPages > 1 ? "flex" : "none";
        flashSetStatus(t("flash.status.done") + " (" + bytesToHuman(flashHexBytes.length) + ")");
    } catch (error) {
        flashSetStatus(t("flash.status.error") + " " + (error && error.message ? error.message : String(error)));
    }
}

async function flashWrite() {
    const targetSelect = document.getElementById("flash_target");
    const startInput = document.getElementById("flash_start");
    if (!targetSelect || !startInput) return;
    if (!targetSelect.value) {
        alert(t("flash.error.no_target"));
        return;
    }
    if (!startInput.value) {
        alert(t("flash.error.bad_range"));
        return;
    }
    if (flashHexBytes.length === 0) {
        alert(t("flash.error.no_data"));
        return;
    }

    const chunks = flashBuildWriteChunks();
    if (chunks.length === 0) {
        alert("No bytes modified since last read/write.");
        return;
    }

    const totalModified = chunks.reduce(function (s, c) { return s + c.count; }, 0);
    var confirmMsg = bytesToHuman(totalModified) + " modified in " + chunks.length + " block(s). Write?";
    if (totalModified > 8192)
        confirmMsg += "\n\nLarge writes may be slow and risky.";
    if (!confirm(confirmMsg)) return;

    try {
        var written = 0;
        for (var ci = 0; ci < chunks.length; ci++) {
            var chunk = chunks[ci];
            flashSetStatus("Writing block " + (ci + 1) + "/" + chunks.length + " (" + chunk.count + "B @ 0x" + (flashReadBase + chunk.start).toString(16) + ")");

            var parts = [];
            for (var j = chunk.start; j <= chunk.end; j++)
                parts.push(flashPadHex(flashHexBytes[j], 2));
            var hexStr = parts.join(" ");

            var formData = new FormData();
            formData.append("op", "write");
            formData.append("storage", "auto");
            formData.append("target", targetSelect.value);
            formData.append("start", "0x" + (flashReadBase + chunk.start).toString(16));
            formData.append("data", hexStr);

            var response = await fetch("/flash/write", { method: "POST", body: formData });
            var responseText = await response.text();
            if (!response.ok) {
                flashSetStatus("Block " + (ci + 1) + " failed: HTTP " + response.status);
                return;
            }
            var payload;
            try { payload = JSON.parse(responseText); } catch (e) {
                flashSetStatus("Block " + (ci + 1) + " parse error");
                return;
            }
            if (!payload || !payload.ok) {
                flashSetStatus("Block " + (ci + 1) + " failed: " + (payload && payload.error ? payload.error : ""));
                return;
            }
            written += chunk.count;
        }
        flashHexModified = new Set();
        flashRenderHexGrid();
        flashRenderHexViews();
        flashSetStatus("Written " + bytesToHuman(written) + " in " + chunks.length + " block(s)");
    } catch (error) {
        flashSetStatus(t("flash.status.error") + " " + (error && error.message ? error.message : String(error)));
    }
}

async function flashErase() {
    const erasePlan = flashBuildErasePlan();
    if (erasePlan.error) {
        alert(erasePlan.error);
        return;
    }
    if (!confirm(t("flash.confirm.erase"))) return;
    const confirmDetail = t("flash.confirm.erase_detail").replace("{device}", erasePlan.deviceName).replace("{detail}", erasePlan.detail);
    if (!confirm(confirmDetail)) return;
    try {
        flashSetStatus(t("flash.status.erasing"));
            const formData = new FormData();
        formData.append("op", "erase");
        formData.append("storage", "auto");
        formData.append("target", erasePlan.target);
        if (erasePlan.hasRange) {
            formData.append("start", "0x" + erasePlan.start.toString(16));
            formData.append("end", "0x" + erasePlan.end.toString(16));
        }
            const response = await fetch("/flash/erase", { method: "POST", body: formData });
            const responseText = await response.text();
        if (!response.ok) {
            flashSetStatus(t("flash.status.http") + " " + response.status + (responseText ? ": " + responseText : ""));
            return;
        }
            let payload;
        try { payload = JSON.parse(responseText); } catch (error) { flashSetStatus(t("flash.status.error") + " parse"); return; }
        if (!payload || !payload.ok) {
            flashSetStatus(t("flash.status.error") + " " + (payload && payload.error ? payload.error : ""));
            return;
        }
        flashSetStatus(t("flash.status.done"));
    } catch (error) {
        flashSetStatus(t("flash.status.error") + " " + (error && error.message ? error.message : String(error)));
    }
}

async function flashRestore() {
    const targetSelect = document.getElementById("flash_target");
    const startInput = document.getElementById("flash_start");
    const endInput = document.getElementById("flash_end");
    const backupInput = document.getElementById("flash_backup");
    let backupFile, baseStart, baseEnd, totalSize;
    const chunkSize = 4 * 1024 * 1024;
    let useChunked;

    function toHex(n) {
        return "0x" + n.toString(16);
    }

    async function sendChunk(blob, chunkOffset, chunkEnd, totalSize, baseStart) {
        return await new Promise(function (resolve, reject) {
                const formData = new FormData();
            formData.append("op", "restore");
            formData.append("backup", blob, "restore_chunk.bin");
            targetSelect && targetSelect.value && formData.append("target", targetSelect.value);
            formData.append("start", toHex(baseStart + chunkOffset));
            formData.append("end", toHex(baseStart + chunkEnd));
            formData.append("storage", "auto");

            const xhr = new XMLHttpRequest();
            xhr.upload.onprogress = function (evt) {
                if (!evt || !evt.lengthComputable) return;
                flashSetProgress((chunkOffset + evt.loaded) / totalSize * 100);
            };
            xhr.upload.onload = function () {
                flashSetProgress((chunkOffset + (chunkEnd - chunkOffset)) / totalSize * 100);
                flashSetStatus(t("flash.status.restoring"));
            };
            xhr.onreadystatechange = function () {
                if (xhr.readyState !== 4) return;
                if (xhr.status !== 200) {
                    flashSetStatus(t("flash.status.http") + " " + xhr.status + (xhr.responseText ? ": " + xhr.responseText : ""));
                    flashSetProgress(null);
                    reject(new Error("http"));
                    return;
                }
                let payload;
                try { payload = JSON.parse(xhr.responseText); } catch (error) {
                    flashSetStatus(t("flash.status.error") + " parse");
                    flashSetProgress(null);
                    reject(error);
                    return;
                }
                if (!payload || !payload.ok) {
                    flashSetStatus(t("flash.status.error") + " " + (payload && payload.error ? payload.error : ""));
                    flashSetProgress(null);
                    reject(new Error("bad"));
                    return;
                }
                if (payload.alert)
                    window.__flash_restore_alert = payload.alert;
                resolve();
            };
            xhr.open("POST", "/flash/restore");
            xhr.send(formData);
        });
    }

    if (!backupInput || !backupInput.files || !backupInput.files.length) {
        alert(t("flash.error.no_file"));
        return;
    }
    if (!confirm(t("flash.confirm.restore"))) return;
    try {
        backupFile = backupInput.files[0];
        totalSize = backupFile ? backupFile.size : 0;
        baseStart = startInput ? parseUserLen(startInput.value) : null;
        baseEnd = endInput ? parseUserLen(endInput.value) : null;
        if ((baseStart === null || baseEnd === null) && backupFile && backupFile.name) {
            const parsedBackup = flashParseBackupFilename(backupFile.name);
            if (parsedBackup) {
                baseStart = parsedBackup.start;
                baseEnd = parsedBackup.end;
                if (targetSelect && !targetSelect.value && parsedBackup.storage && parsedBackup.target)
                    flashSelectTarget(parsedBackup.storage + ":" + parsedBackup.target);
                startInput && (startInput.value = toHex(baseStart));
                endInput && (endInput.value = toHex(baseEnd));
            }
        }
        if (baseStart === null || baseEnd === null || baseEnd <= baseStart) {
            flashSetStatus(t("flash.error.bad_range"));
            return;
        }
        if ((baseEnd - baseStart) !== totalSize) {
            flashSetStatus(t("flash.error.bad_range"));
            return;
        }

        useChunked = totalSize > chunkSize;
        flashSetProgress(0);
        flashSetStatus(t("flash.status.uploading"));

        if (!useChunked) {
            await sendChunk(backupFile, 0, totalSize, totalSize, baseStart);
        } else {
            let offset = 0;
            while (offset < totalSize) {
                const next = Math.min(offset + chunkSize, totalSize);
                const blob = backupFile.slice(offset, next);
                await sendChunk(blob, offset, next, totalSize, baseStart);
                offset = next;
            }
        }

        flashSetProgress(100);
        flashSetStatus(t("flash.status.done"));
        alert(t("flash.status.restored", window.__flash_restore_alert || "Backup restore completed."));
        window.__flash_restore_alert = "";
    } catch (error) {
        flashSetStatus(t("flash.status.error") + " " + (error && error.message ? error.message : String(error)));
    }
}
