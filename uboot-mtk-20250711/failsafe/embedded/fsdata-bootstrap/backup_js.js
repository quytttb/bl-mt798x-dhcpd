/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 */

function setBackupStatus(message) {
    const statusElement = document.getElementById("backup_status");
    statusElement && (statusElement.style.display = message ? "block" : "none", statusElement.textContent = message || "")
}

function setBackupProgress(percent) {
    const progressElement = document.getElementById("bar");
    if (!progressElement) return;
    const boundedPercent = Math.max(0, Math.min(100, parseInt(percent || 0)));
    progressElement.style.display = "block";
    progressElement.style.setProperty("--percent", boundedPercent);
}

function backupUpdateRangeHint() {
    const rangeHintElement = document.getElementById("backup_range_hint");
    if (!rangeHintElement) return;
    const startValue = parseUserLen(document.getElementById("backup_start").value);
    const endValue = parseUserLen(document.getElementById("backup_end").value);
    if (startValue === null || endValue === null) {
        rangeHintElement.textContent = t("backup.range.hint");
    } else {
        const rangeSize = endValue >= startValue ? endValue - startValue : 0;
        rangeHintElement.textContent = `Start=${bytesToHuman(startValue)}, End=${bytesToHuman(endValue)}, Size=${bytesToHuman(rangeSize)}`;
    }
}

function backupRefreshI18n() {
    const targetSelect = document.getElementById("backup_target");
    if (!targetSelect) return;
    for (let optionIndex = 0; optionIndex < targetSelect.options.length; optionIndex++) {
        const optionElement = targetSelect.options[optionIndex];
        if (optionElement && optionElement.dataset && optionElement.dataset.i18nKey) optionElement.textContent = window.t(optionElement.dataset.i18nKey);
    }
    for (let optionIndex = 0; optionIndex < targetSelect.options.length; optionIndex++) {
        const optionElement = targetSelect.options[optionIndex];
        if (!optionElement || !optionElement.dataset) continue;
        if (optionElement.dataset.kind === "mtd-full") {
            const mtdName = optionElement.dataset.mtdName || "";
            optionElement.textContent = `[MTD] ${window.t("backup.target.full_disk")}${mtdName ? ` (${mtdName})` : ""}${optionElement.dataset.size ? ` (${bytesToHuman(parseInt(optionElement.dataset.size, 10))})` : ""}`;
        }
        if (optionElement.dataset.kind === "mtd-oob") {
            const mtdName = optionElement.dataset.mtdName || "";
            optionElement.textContent = `[MTD] ${window.t("backup.target.full_disk")}${mtdName ? ` (${mtdName})` : ""} (OOB${optionElement.dataset.size ? ` ${bytesToHuman(parseInt(optionElement.dataset.size, 10))}` : ""})`;
        }
    }
}

function backupInit() {
    const modeSelect = document.getElementById("backup_mode");
    const rangeContainer = document.getElementById("backup_range");
    const targetSelect = document.getElementById("backup_target");
    const targetField = document.getElementById("backup_target_field");
    const targetRow = document.getElementById("backup_mode_target_row");
    let updateBackupUi, startInput, endInput;
    let backupInfoData = null;
    let nandRawSize = 0;
    let ramAvailable = 0;

    function selectTargetByValue(targetValue) {
        for (let optionIndex = 0; optionIndex < targetSelect.options.length; optionIndex++) if (targetSelect.options[optionIndex].value === targetValue) return targetSelect.selectedIndex = optionIndex, true;
        return false
    }
    function selectTargetByKind(targetKind) {
        for (let optionIndex = 0; optionIndex < targetSelect.options.length; optionIndex++) if (targetSelect.options[optionIndex].dataset && targetSelect.options[optionIndex].dataset.kind === targetKind) return targetSelect.selectedIndex = optionIndex, true;
        return false
    }
    function selectFirstNonEmptyTarget() {
        for (let optionIndex = 0; optionIndex < targetSelect.options.length; optionIndex++) if (targetSelect.options[optionIndex].value) {
            targetSelect.selectedIndex = optionIndex;
            return true
        }
        return false
    }
    function ensureValidTargetSelection() {
        let selectedOption, selectedKind;
        if (!targetSelect || targetSelect.options.length <= 1) return;
        selectedOption = targetSelect.options[targetSelect.selectedIndex];
        selectedKind = selectedOption && selectedOption.dataset ? selectedOption.dataset.kind : "";
        (selectedKind === "mmc-part" || selectedKind === "mtd-part" || !targetSelect.value) && (selectTargetByValue("mmc:raw") || selectTargetByKind("mtd-full") || selectFirstNonEmptyTarget())
    }
    modeSelect && rangeContainer && targetSelect && (updateBackupUi = function () {
        const isRangeMode = modeSelect.value === "range";
        isRangeMode ? (rangeContainer.style.display = "block", ensureValidTargetSelection(), backupUpdateRangeHint()) : (rangeContainer.style.display = "none");
        targetField && (targetField.style.display = isRangeMode ? "none" : "");
        targetRow && (targetRow.style.gridTemplateColumns = isRangeMode ? "1fr" : "");
    }, modeSelect.onchange = updateBackupUi, startInput = document.getElementById("backup_start"), endInput = document.getElementById("backup_end"), startInput && (startInput.oninput = backupUpdateRangeHint), endInput && (endInput.oninput = backupUpdateRangeHint), updateBackupUi(), setBackupStatus(""), ajax({
        url: "/backup/info",
        done: function (responseText) {
            let backupInfo;
            let infoElement;
            let optionElement;
            let rawOption;
            let fullDiskOption;
            try {
                backupInfo = JSON.parse(responseText)
            } catch (error) {
                setBackupStatus("backupinfo parse failed");
                return
            }
            backupInfoData = backupInfo;

            // Store NAND raw metadata
            if (backupInfo.mtd && backupInfo.mtd.nand_raw_size) {
                nandRawSize = backupInfo.mtd.nand_raw_size;
                ramAvailable = backupInfo.mtd.ram_available || 0;
            }

            infoElement = document.getElementById("backup_info");
            infoElement && (optionElement = [], backupInfo.mmc && backupInfo.mmc.present ? optionElement.push("MMC: " + (backupInfo.mmc.vendor || "") + " " + (backupInfo.mmc.product || "")) : optionElement.push("MMC: " + t("backup.storage.not_present")), backupInfo.mtd && backupInfo.mtd.present ? optionElement.push("MTD: " + (backupInfo.mtd.model || "")) : optionElement.push("MTD: " + t("backup.storage.not_present")), infoElement.textContent = optionElement.join(" | "));
            targetSelect.options.length = 0;
            optionElement = document.createElement("option");
            optionElement.value = "";
            optionElement.dataset.i18nKey = "backup.target.placeholder";
            targetSelect.appendChild(optionElement);
            backupInfo.mmc && backupInfo.mmc.present && (rawOption = document.createElement("option"), rawOption.value = "mmc:raw", rawOption.textContent = "[MMC] raw", rawOption.dataset.kind = "mmc-raw", targetSelect.appendChild(rawOption), backupInfo.mmc.parts && backupInfo.mmc.parts.length && backupInfo.mmc.parts.forEach(function (partition) {
                let partOption;
                partition && partition.name && (partOption = document.createElement("option"), partOption.value = "mmc:" + partition.name, partOption.textContent = "[MMC] " + partition.name + (partition.size ? " (" + bytesToHuman(partition.size) + ")" : ""), partOption.dataset.kind = "mmc-part", targetSelect.appendChild(partOption))
            }));

            if (backupInfo.mtd && backupInfo.mtd.present && backupInfo.mtd.parts && backupInfo.mtd.parts.length) {
                let mtdType = backupInfo.mtd.type;
                let hasMasterPartitions = mtdType === 3 || mtdType === 4 || mtdType === 8;
                let masterPartitions = [];
                hasMasterPartitions && backupInfo.mtd.parts.forEach(function (partition) {
                    partition && partition.name && partition.master && masterPartitions.push(partition)
                });

                hasMasterPartitions && masterPartitions.length && masterPartitions.forEach(function (partition) {
                    const fullDiskOptionElement = document.createElement("option");
                    fullDiskOptionElement.value = "mtd:" + partition.name;
                    fullDiskOptionElement.dataset.mtdName = partition.name;
                    fullDiskOptionElement.dataset.size = partition.size ? String(partition.size) : "";
                    fullDiskOptionElement.dataset.kind = "mtd-full";
                    targetSelect.appendChild(fullDiskOptionElement);

                    /* OOB raw dump option — only for physical NAND, skip NMBM layer */
                    if (nandRawSize > 0 && ramAvailable > 0 && nandRawSize <= ramAvailable
                        && !partition.name.startsWith("nmbm")) {
                        const oobOptionElement = document.createElement("option");
                        oobOptionElement.value = "mtd:" + partition.name;
                        oobOptionElement.dataset.mtdName = partition.name;
                        oobOptionElement.dataset.size = String(nandRawSize);
                        oobOptionElement.dataset.kind = "mtd-oob";
                        oobOptionElement.dataset.raw = "1";
                        targetSelect.appendChild(oobOptionElement);
                    }
                });

                backupInfo.mtd.parts.forEach(function (partition) {
                    let partitionOption;
                    if (!partition || !partition.name) return;
                    if (hasMasterPartitions && partition.master) return;
                    partitionOption = document.createElement("option");
                    partitionOption.value = "mtd:" + partition.name;
                    partitionOption.textContent = "[MTD] " + partition.name + (partition.size ? " (" + bytesToHuman(partition.size) + ")" : "");
                    partitionOption.dataset.kind = "mtd-part";
                    targetSelect.appendChild(partitionOption)
                })
            }
            targetSelect.options.length > 1 && (targetSelect.selectedIndex = 1);
            backupRefreshI18n();
            updateBackupUi && updateBackupUi();
        }
    }))
}

async function startBackup() {
    const modeSelect = document.getElementById("backup_mode");
    const targetSelect = document.getElementById("backup_target");
    let backupMode, targetValue, formData, response, contentLength, expectedLength, downloadName, downloadedBytes, saveHandle, writableStream, reader, chunk, bufferedChunks;
    let isRawMode = false;
    if (!modeSelect || !targetSelect) return;
    backupMode = modeSelect.value;
    targetValue = targetSelect.value;
    if (!targetValue) {
        alert(t("backup.error.no_target"));
        return;
    }

    /* Detect OOB mode from target option data-raw attribute */
    {
        const selOpt = targetSelect.options[targetSelect.selectedIndex];
        if (selOpt && selOpt.dataset && selOpt.dataset.raw === "1")
            isRawMode = true;
    }

    formData = new FormData();
    formData.append("mode", backupMode);
    formData.append("storage", "auto");
    formData.append("target", targetValue);
    if (isRawMode)
        formData.append("raw", "1");
    if (backupMode === "range") {
        const startInput = document.getElementById("backup_start");
        const endInput = document.getElementById("backup_end");
        if (!startInput || !endInput || !startInput.value || !endInput.value) {
            alert(t("backup.error.bad_range"));
            return;
        }
        formData.append("start", startInput.value);
        formData.append("end", endInput.value);
    }
    setBackupProgress(0);
    setBackupStatus(t("backup.status.starting"));
    try {
        response = await fetch("/backup/main", { method: "POST", body: formData });
        if (!response.ok) {
            setBackupStatus(t("backup.error.http") + " " + response.status);
            return
        }
        contentLength = response.headers.get("Content-Length");
        expectedLength = contentLength ? parseInt(contentLength, 10) : 0;
        downloadName = parseFilenameFromDisposition(response.headers.get("Content-Disposition"));
        downloadName || (downloadName = "backup.bin");
        // Ensure we have board info for filename even on pages without #sysinfo
        await ensureSysInfoLoaded();
        downloadName = makeBackupDownloadName(downloadName);
        downloadedBytes = 0;
        if (window.showSaveFilePicker) {
            saveHandle = await window.showSaveFilePicker({ suggestedName: downloadName, types: [{ description: "Binary", accept: { "application/octet-stream": [".bin"] } }] });
            writableStream = await saveHandle.createWritable();
            reader = response.body.getReader();
            while (true) {
                chunk = await reader.read();
                if (chunk.done) break;
                await writableStream.write(chunk.value);
                downloadedBytes += chunk.value.length;
                expectedLength ? setBackupProgress(downloadedBytes / expectedLength * 100) : setBackupProgress(0);
                setBackupStatus(t("backup.status.downloading") + " " + bytesToHuman(downloadedBytes) + (expectedLength ? " / " + bytesToHuman(expectedLength) : ""))
            }
            await writableStream.close();
            setBackupProgress(100);
            setBackupStatus(t("backup.status.done") + " " + downloadName)
        } else {
            bufferedChunks = [];
            reader = response.body.getReader();
            while (true) {
                chunk = await reader.read();
                if (chunk.done) break;
                bufferedChunks.push(chunk.value);
                downloadedBytes += chunk.value.length;
                expectedLength ? setBackupProgress(downloadedBytes / expectedLength * 100) : setBackupProgress(0);
                setBackupStatus(t("backup.status.downloading") + " " + bytesToHuman(downloadedBytes) + (expectedLength ? " / " + bytesToHuman(expectedLength) : ""))
            }
            setBackupProgress(100);
            setBackupStatus(t("backup.status.preparing"));
            const backupBlob = new Blob(bufferedChunks, { type: "application/octet-stream" });
            const downloadLink = document.createElement("a");
            downloadLink.href = URL.createObjectURL(backupBlob);
            downloadLink.download = downloadName;
            document.body.appendChild(downloadLink);
            downloadLink.click();
            document.body.removeChild(downloadLink);
            setBackupStatus(t("backup.status.done") + " " + downloadName)
        }
    } catch (error) {
        setBackupStatus(t("backup.error.exception") + " " + (error && error.message ? error.message : String(error)))
    }
}
