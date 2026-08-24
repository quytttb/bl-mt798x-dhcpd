/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 *
 * UBI Volume Management Frontend Logic
 */

(function () {
    "use strict";

    var ubiData = null;
    var mtdList = [];

    function setStatus(msg, isError) {
        var el = document.getElementById("ubi_status");
        if (!el) return;
        el.textContent = msg || "";
        el.className = "settings-status" + (isError ? " red" : "");
    }

    function bytesToHuman(bytes) {
        if (bytes == null || bytes === 0) return "0 B";
        var units = ["B", "KiB", "MiB", "GiB"];
        var i = 0;
        var size = Number(bytes);
        while (size >= 1024 && i < units.length - 1) {
            size /= 1024;
            i++;
        }
        return size.toFixed(i === 0 ? 0 : 2) + " " + units[i];
    }

    function renderDeviceInfo(data) {
        var el = document.getElementById("ubi_device_info");
        if (!el) return;

        if (!data || data.attached === false) {
            el.innerHTML = '<div class="sysinfo-line">' + t("ubi.status.no_device") + '</div>';
            return;
        }

        var html = '';
        html += '<div class="sysinfo-line">' + t("ubi.info.mtd") + ' ' + (data.mtd_name || "-") + '</div>';
        html += '<div class="sysinfo-line">' + t("ubi.info.peb_size") + ' ' + bytesToHuman(data.peb_size) + '</div>';
        html += '<div class="sysinfo-line">' + t("ubi.info.leb_size") + ' ' + bytesToHuman(data.leb_size) + '</div>';
        html += '<div class="sysinfo-line">' + t("ubi.info.good_peb") + ' ' + data.good_peb_count + '</div>';
        html += '<div class="sysinfo-line">' + t("ubi.info.bad_peb") + ' ' + data.bad_peb_count + '</div>';
        html += '<div class="sysinfo-line">' + t("ubi.info.avail_peb") + ' ' + data.avail_pebs + '</div>';
        html += '<div class="sysinfo-line">' + t("ubi.info.vol_count") + ' ' + data.vol_count + ' / ' + data.max_vol_count + '</div>';
        html += '<div class="sysinfo-line">' + t("ubi.info.max_ec") + ' ' + data.max_ec + ' / ' + data.mean_ec + '</div>';
        // Expandable details
        html += '<details class="sysinfo-details">';
        html += '<summary>' + t("ubi.info.more", "More info") + '</summary>';
        html += '<div class="sysinfo-extra">';
        html += '<div class="sysinfo-line">' + t("ubi.info.flash_size", "Flash size:") + ' ' + bytesToHuman(data.flash_size) + '</div>';
        html += '<div class="sysinfo-line">' + t("ubi.info.ubi_num", "UBI number:") + ' ' + (data.ubi_num != null ? data.ubi_num : "-") + '</div>';
        html += '<div class="sysinfo-line">' + t("ubi.info.min_io", "Min I/O size:") + ' ' + bytesToHuman(data.min_io_size) + '</div>';
        html += '<div class="sysinfo-line">' + t("ubi.info.rsvd_pebs", "Reserved PEBs:") + ' ' + (data.rsvd_pebs != null ? data.rsvd_pebs : "-") + '</div>';
        html += '<div class="sysinfo-line">' + t("ubi.info.beb_rsvd", "BEB reserved:") + ' ' + (data.beb_rsvd_pebs != null ? data.beb_rsvd_pebs : "-") + '</div>';
        html += '</div></details>';
        el.innerHTML = html;
    }

    function renderVolumeList(volumes) {
        var tbody = document.getElementById("ubi_volume_tbody");
        if (!tbody) return;

        renderWriteVolSelect(volumes);

        if (!volumes || volumes.length === 0) {
            tbody.innerHTML = '<tr><td colspan="8" class="table-empty" data-i18n="ubi.no_volumes">' + t("ubi.no_volumes") + '</td></tr>';
            return;
        }

        var html = '';
        for (var i = 0; i < volumes.length; i++) {
            var vol = volumes[i];
            var statusClass = vol.corrupted ? "red" : "";
            var statusText = vol.corrupted ? t("ubi.status.corrupted") :
                            (vol.upd_marker ? t("ubi.status.updating") : t("ubi.status.ok"));
            var crcText = vol.skip_check ? t("ubi.status.crc_skip") : t("ubi.status.crc_check");
            var crcClass = vol.skip_check ? "red" : "";

            html += '<tr>';
            html += '<td class="col-num">' + vol.id + '</td>';
            html += '<td class="col-name">' + escapeHtml(vol.name) + '</td>';
            html += '<td class="col-size">' + bytesToHuman(vol.size) + '</td>';
            html += '<td class="col-size">' + bytesToHuman(vol.used_bytes) + '</td>';
            html += '<td>' + t("ubi.type." + vol.type) + '</td>';
            html += '<td class="' + statusClass + '">' + statusText + '</td>';
            html += '<td class="' + crcClass + '">' + crcText + ' <button class="button button-sm" onclick="ubiToggleSkipCheck(\'' + escapeHtml(vol.name) + '\',' + (vol.skip_check ? "true" : "false") + ')">' + (vol.skip_check ? t("ubi.btn.crc_enable") : t("ubi.btn.crc_disable")) + '</button></td>';
            html += '<td>';
            html += '<button class="button button-sm" onclick="ubiCheckVol(\'' + escapeHtml(vol.name) + '\')">' + t("ubi.btn.check") + '</button> ';
            html += '<button class="button button-sm" onclick="ubiWriteVol(\'' + escapeHtml(vol.name) + '\')">' + t("ubi.btn.write") + '</button> ';
            html += '<button class="button button-sm" onclick="ubiBackupVol(\'' + escapeHtml(vol.name) + '\')">' + t("ubi.btn.backup") + '</button> ';
            html += '<button class="button button-sm" onclick="ubiRenameVol(\'' + escapeHtml(vol.name) + '\')">' + t("ubi.btn.rename") + '</button> ';
            html += '<button class="button button-danger button-sm" onclick="ubiRemoveVol(\'' + escapeHtml(vol.name) + '\')">' + t("ubi.btn.remove") + '</button>';
            html += '</td>';
            html += '</tr>';
        }
        tbody.innerHTML = html;
    }

    function escapeHtml(str) {
        if (!str) return "";
        return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/'/g, "&#039;");
    }

    function fetchUbiInfo() {
        var infoEl = document.getElementById("ubi_device_info");
        if (infoEl) infoEl.innerHTML = '<div class="sysinfo-line">' + t("ubi.loading") + '</div>';
        ajax({
            url: "/ubi/info",
            done: function (resp) {
                try {
                    ubiData = JSON.parse(resp);
                } catch (e) {
                    setStatus(t("ubi.error.parse"), true);
                    return;
                }
                renderDeviceInfo(ubiData);
                if (ubiData && ubiData.attached) {
                    fetchVolumeList();
                } else {
                    renderVolumeList([]);
                    setStatus("");
                }
            }
        });
    }

    function fetchVolumeList() {
        ajax({
            url: "/ubi/volumes",
            done: function (resp) {
                try {
                    var data = JSON.parse(resp);
                    renderVolumeList(data.volumes || []);
                    setStatus("");
                } catch (e) {
                    setStatus(t("ubi.error.parse"), true);
                }
            }
        });
    }

    function fetchMtdList() {
        ajax({
            url: "/ubi/mtd_list",
            done: function (resp) {
                try {
                    var data = JSON.parse(resp);
                    mtdList = data.partitions || [];
                } catch (e) {
                    mtdList = [];
                }
                renderMtdSelect();
            }
        });
    }

    function renderMtdSelect() {
        var select = document.getElementById("mtd_select");
        if (!select) return;

        select.innerHTML = '<option value="" data-i18n="ubi.attach.select_mtd">' + t("ubi.attach.select_mtd") + '</option>';
        var ubiIndex = -1;
        for (var i = 0; i < mtdList.length; i++) {
            var mtd = mtdList[i];
            var opt = document.createElement("option");
            opt.value = mtd.name;
            opt.textContent = mtd.name + " (" + bytesToHuman(mtd.size) + ")";
            select.appendChild(opt);
            if (mtd.name === "ubi") ubiIndex = i;
        }
        // Auto-select "ubi" partition if it exists
        if (ubiIndex >= 0) {
            select.selectedIndex = ubiIndex + 1; // +1 for the placeholder option
        }
    }

    function renderWriteVolSelect(volumes) {
        var select = document.getElementById("write_vol_select");
        if (!select) return;
        select.innerHTML = '<option value="" data-i18n="ubi.write.select">' + t("ubi.write.select") + '</option>';
        if (!volumes) return;
        for (var i = 0; i < volumes.length; i++) {
            var opt = document.createElement("option");
            opt.value = volumes[i].name;
            opt.textContent = volumes[i].name + " (" + bytesToHuman(volumes[i].size) + ")";
            select.appendChild(opt);
        }
    }

    function attachMtd() {
        var select = document.getElementById("mtd_select");
        var mtdName = select ? select.value : "";
        if (!mtdName) {
            setStatus(t("ubi.error.select_mtd"), true);
            return;
        }

        setStatus(t("ubi.status.attaching"));
        var formData = new FormData();
        formData.append("mtd_name", mtdName);

        ajax({
            url: "/ubi/attach",
            data: formData,
            done: function (resp) {
                try {
                    var data = JSON.parse(resp);
                    if (data.ok) {
                        setStatus(t("ubi.status.attached"));
                        fetchUbiInfo();
                    } else {
                        setStatus(data.error || t("ubi.error.attach_failed"), true);
                    }
                } catch (e) {
                    setStatus(t("ubi.error.parse"), true);
                }
            }
        });
    }

    function detachUbi() {
        setStatus(t("ubi.status.detaching"));
        var formData = new FormData();

        ajax({
            url: "/ubi/detach",
            data: formData,
            done: function (resp) {
                try {
                    var data = JSON.parse(resp);
                    if (data.ok) {
                        setStatus(t("ubi.status.detached"));
                        ubiData = null;
                        renderDeviceInfo(null);
                        renderVolumeList([]);
                    } else {
                        setStatus(data.error || t("ubi.error.detach_failed"), true);
                    }
                } catch (e) {
                    setStatus(t("ubi.error.parse"), true);
                }
            }
        });
    }

    function createVolume() {
        var nameInput = document.getElementById("vol_name");
        var sizeInput = document.getElementById("vol_size");
        var typeSelect = document.getElementById("vol_type");
        var skipcheckInput = document.getElementById("vol_skipcheck");

        var name = nameInput ? nameInput.value.trim() : "";
        if (!name) {
            setStatus(t("ubi.error.name_required"), true);
            return;
        }

        setStatus(t("ubi.status.creating"));
        var formData = new FormData();
        formData.append("name", name);
        if (sizeInput && sizeInput.value.trim()) {
            formData.append("size", sizeInput.value.trim());
        }
        formData.append("type", typeSelect ? typeSelect.value : "dynamic");
        formData.append("skipcheck", skipcheckInput && skipcheckInput.checked ? "1" : "0");

        ajax({
            url: "/ubi/create",
            data: formData,
            done: function (resp) {
                try {
                    var data = JSON.parse(resp);
                    if (data.ok) {
                        setStatus(t("ubi.status.created"));
                        if (nameInput) nameInput.value = "";
                        if (sizeInput) sizeInput.value = "";
                        fetchVolumeList();
                        fetchUbiInfo();
                    } else {
                        setStatus(data.error || t("ubi.error.create_failed"), true);
                    }
                } catch (e) {
                    setStatus(t("ubi.error.parse"), true);
                }
            }
        });
    }

    function removeVolume(name) {
        if (!confirm(t("ubi.confirm.remove", "Remove volume \'$1\'?").replace("$1", name))) {
            return;
        }

        setStatus(t("ubi.status.removing"));
        var formData = new FormData();
        formData.append("name", name);

        ajax({
            url: "/ubi/remove",
            data: formData,
            done: function (resp) {
                try {
                    var data = JSON.parse(resp);
                    if (data.ok) {
                        setStatus(t("ubi.status.removed"));
                        fetchVolumeList();
                        fetchUbiInfo();
                    } else {
                        setStatus(data.error || t("ubi.error.remove_failed"), true);
                    }
                } catch (e) {
                    setStatus(t("ubi.error.parse"), true);
                }
            }
        });
    }

    function renameVolume(oldName) {
        var newName = prompt(t("ubi.prompt.new_name", "New volume name:"), oldName);
        if (!newName || newName === oldName) {
            return;
        }

        setStatus(t("ubi.status.renaming"));
        var formData = new FormData();
        formData.append("old_name", oldName);
        formData.append("new_name", newName);

        ajax({
            url: "/ubi/rename",
            data: formData,
            done: function (resp) {
                try {
                    var data = JSON.parse(resp);
                    if (data.ok) {
                        setStatus(t("ubi.status.renamed"));
                        fetchVolumeList();
                    } else {
                        setStatus(data.error || t("ubi.error.rename_failed"), true);
                    }
                } catch (e) {
                    setStatus(t("ubi.error.parse"), true);
                }
            }
        });
    }

    function checkVolume(name) {
        setStatus(t("ubi.status.checking"));
        var formData = new FormData();
        formData.append("name", name);

        ajax({
            url: "/ubi/check",
            data: formData,
            done: function (resp) {
                try {
                    var data = JSON.parse(resp);
                    if (data.exists) {
                        setStatus(t("ubi.check.exists").replace("$1", name));
                    } else {
                        setStatus(t("ubi.check.not_exists").replace("$1", name), true);
                    }
                } catch (e) {
                    setStatus(t("ubi.error.parse"), true);
                }
            }
        });
    }

    function toggleSkipCheck(name, skip) {
        setStatus(t("ubi.status.skipcheck_updating"));
        var formData = new FormData();
        formData.append("name", name);
        // Target mode = inverse of the current state
        formData.append("mode", skip ? "0" : "1");

        ajax({
            url: "/ubi/skipcheck",
            data: formData,
            done: function (resp) {
                try {
                    var data = JSON.parse(resp);
                    if (data.ok) {
                        setStatus(data.skip_check ? t("ubi.status.skipcheck_on") : t("ubi.status.skipcheck_off"));
                        fetchVolumeList();
                    } else {
                        setStatus(data.error || t("ubi.error.skipcheck_failed"), true);
                    }
                } catch (e) {
                    setStatus(t("ubi.error.parse"), true);
                }
            }
        });
    }

    function writeVolume() {
        var select = document.getElementById("write_vol_select");
        var fileInput = document.getElementById("write_file");
        var offsetInput = document.getElementById("write_offset");
        var fullSizeInput = document.getElementById("write_full_size");

        var volName = select ? select.value : "";
        if (!volName) {
            setWriteStatus(t("ubi.error.no_write_vol"), true);
            return;
        }
        var file = fileInput && fileInput.files && fileInput.files[0];
        if (!file) {
            setWriteStatus(t("ubi.error.no_file"), true);
            return;
        }

        setWriteProgress(0);
        setWriteStatus(t("ubi.status.writing") + " " + bytesToHuman(file.size));

        var formData = new FormData();
        formData.append("name", volName);
        formData.append("data", file);
        if (offsetInput && offsetInput.value.trim()) {
            formData.append("offset", offsetInput.value.trim());
        }
        if (fullSizeInput && fullSizeInput.value.trim()) {
            formData.append("full_size", fullSizeInput.value.trim());
        }

        ajax({
            url: "/ubi/write",
            data: formData,
            progress: function (event) {
                if (event.lengthComputable) {
                    setWriteProgress(event.loaded / event.total * 100);
                }
            },
            done: function (resp) {
                try {
                    var data = JSON.parse(resp);
                    if (data.ok) {
                        setWriteProgress(100);
                        setWriteStatus(t("ubi.status.written"));
                        fetchVolumeList();
                        fetchUbiInfo();
                    } else {
                        setWriteStatus(data.error || t("ubi.error.write_failed"), true);
                    }
                } catch (e) {
                    setWriteStatus(t("ubi.error.parse"), true);
                }
            }
        });
    }

    function writeVolumeTo(name) {
        var select = document.getElementById("write_vol_select");
        if (select) {
            for (var i = 0; i < select.options.length; i++) {
                if (select.options[i].value === name) {
                    select.selectedIndex = i;
                    break;
                }
            }
        }
        var fileInput = document.getElementById("write_file");
        if (fileInput) fileInput.focus();
        setStatus("");
    }

    function setBackupStatus(msg) {
        var el = document.getElementById("ubi_backup_status");
        if (!el) return;
        el.style.display = msg ? "block" : "none";
        el.textContent = msg || "";
    }

    function setBackupProgress(percent) {
        var el = document.getElementById("ubi_backup_bar");
        if (!el) return;
        var p = Math.max(0, Math.min(100, parseInt(percent || 0)));
        el.style.display = "block";
        el.style.setProperty("--percent", p);
    }

    function setWriteStatus(msg, isError) {
        var el = document.getElementById("ubi_write_status");
        if (!el) return;
        el.style.display = msg ? "block" : "none";
        el.textContent = msg || "";
        el.className = isError ? "red" : "";
    }

    function setWriteProgress(percent) {
        var el = document.getElementById("ubi_write_bar");
        if (!el) return;
        var p = Math.max(0, Math.min(100, parseInt(percent || 0)));
        el.style.display = "block";
        el.style.setProperty("--percent", p);
    }

    async function backupVolume(name) {
        setBackupProgress(0);
        setBackupStatus(t("backup.status.starting"));
        try {
            var formData = new FormData();
            formData.append("name", name);
            var response = await fetch("/ubi/backup", { method: "POST", body: formData });
            if (!response.ok) {
                setBackupStatus(t("backup.error.http") + " " + response.status);
                return;
            }
            var contentLength = response.headers.get("Content-Length");
            var expectedLength = contentLength ? parseInt(contentLength, 10) : 0;
            var downloadName = parseFilenameFromDisposition(response.headers.get("Content-Disposition"));
            downloadName || (downloadName = "ubi_" + name + ".bin");
            await ensureSysInfoLoaded();
            downloadName = makeBackupDownloadName(downloadName);
            var downloadedBytes = 0;

            if (window.showSaveFilePicker) {
                var saveHandle = await window.showSaveFilePicker({
                    suggestedName: downloadName,
                    types: [{ description: "Binary", accept: { "application/octet-stream": [".bin"] } }]
                });
                var writableStream = await saveHandle.createWritable();
                var reader = response.body.getReader();
                while (true) {
                    var chunk = await reader.read();
                    if (chunk.done) break;
                    await writableStream.write(chunk.value);
                    downloadedBytes += chunk.value.length;
                    expectedLength ? setBackupProgress(downloadedBytes / expectedLength * 100) : setBackupProgress(0);
                    setBackupStatus(t("backup.status.downloading") + " " + bytesToHuman(downloadedBytes) + (expectedLength ? " / " + bytesToHuman(expectedLength) : ""));
                }
                await writableStream.close();
            } else {
                var bufferedChunks = [];
                var reader = response.body.getReader();
                while (true) {
                    var chunk = await reader.read();
                    if (chunk.done) break;
                    bufferedChunks.push(chunk.value);
                    downloadedBytes += chunk.value.length;
                    expectedLength ? setBackupProgress(downloadedBytes / expectedLength * 100) : setBackupProgress(0);
                    setBackupStatus(t("backup.status.downloading") + " " + bytesToHuman(downloadedBytes) + (expectedLength ? " / " + bytesToHuman(expectedLength) : ""));
                }
                setBackupProgress(100);
                setBackupStatus(t("backup.status.preparing"));
                var backupBlob = new Blob(bufferedChunks, { type: "application/octet-stream" });
                var a = document.createElement("a");
                a.href = URL.createObjectURL(backupBlob);
                a.download = downloadName;
                document.body.appendChild(a);
                a.click();
                document.body.removeChild(a);
            }
            setBackupProgress(100);
            setBackupStatus(t("backup.status.done") + " " + downloadName);
        } catch (error) {
            setBackupStatus(t("backup.error.exception") + " " + (error && error.message ? error.message : String(error)));
        }
    }

    // Global functions for onclick handlers
    window.ubiRemoveVol = removeVolume;
    window.ubiRenameVol = renameVolume;
    window.ubiBackupVol = backupVolume;
    window.ubiCheckVol = checkVolume;
    window.ubiWriteVol = writeVolumeTo;
    window.ubiToggleSkipCheck = toggleSkipCheck;

    // Initialize
    window.ubiInit = function () {
        fetchUbiInfo();
        fetchMtdList();

        // Button handlers
        var btnRefresh = document.getElementById("btn_refresh");
        if (btnRefresh) {
            btnRefresh.addEventListener("click", function () {
                fetchUbiInfo();
                fetchMtdList();
            });
        }

        var btnAttach = document.getElementById("btn_attach");
        if (btnAttach) {
            btnAttach.addEventListener("click", attachMtd);
        }

        var btnDetach = document.getElementById("btn_detach");
        if (btnDetach) {
            btnDetach.addEventListener("click", detachUbi);
        }

        var btnCreate = document.getElementById("btn_create");
        if (btnCreate) {
            btnCreate.addEventListener("click", createVolume);
        }

        var btnWrite = document.getElementById("btn_write");
        if (btnWrite) {
            btnWrite.addEventListener("click", writeVolume);
        }
    };
})();
