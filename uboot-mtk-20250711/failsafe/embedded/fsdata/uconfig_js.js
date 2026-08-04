/* SPDX-License-Identifier: GPL-2.0 */

const UCONFIG_IMAGE_SIZE = 0x80000;

function uconfigSetMessage(message, isError) {
    const element = document.getElementById("uconfig_message");
    if (!element) return;
    element.style.display = message ? "block" : "none";
    element.textContent = message || "";
    element.classList.toggle("red", !!isError);
}

function uconfigPass(value) {
    return value ? t("uconfig.status.pass", "PASS") : t("uconfig.status.fail", "FAIL");
}

function uconfigFormat(key, fallback, value) {
    return t(key, fallback).replace("$1", value);
}

function uconfigStatusFact(label, value) {
    const fact = document.createElement("span");
    const state = document.createElement("span");
    fact.className = "uconfig-status-fact";
    fact.append(`${label}: `);
    state.className = `uconfig-status-state ${value ? "is-pass" : "is-fail"}`;
    state.textContent = uconfigPass(value);
    fact.append(state);
    return fact;
}

function uconfigRenderRow(elementId, label, facts) {
    const element = document.getElementById(elementId);
    if (!element) return;
    const title = document.createElement("strong");
    const values = document.createElement("span");
    title.className = "uconfig-status-title";
    title.textContent = label;
    values.className = "uconfig-status-facts";
    facts.forEach(([factLabel, value]) => values.append(uconfigStatusFact(factLabel, value)));
    element.replaceChildren(title, values);
}

function uconfigRenderSlot(elementId, label, slot) {
    if (!slot) return;
    uconfigRenderRow(elementId, label, [
        [t("uconfig.fact.identity", "Identity"), slot.identity_valid],
        [t("uconfig.fact.crc_a", "CRC A"), slot.crc?.[0]],
        [t("uconfig.fact.crc_b", "CRC B"), slot.crc?.[1]],
        [t("uconfig.fact.mirror", "Mirror"), slot.redundant],
        [t("uconfig.fact.vn", "VN"), slot.country_vn],
        [t("uconfig.fact.defaults", "Defaults"), slot.recovery_defaults],
    ]);
}

async function uconfigReadJson(response) {
    let body;
    try {
        body = await response.json();
    } catch (error) {
        throw new Error(uconfigFormat("uconfig.error.invalid_response", "Invalid response ($1)", response.status));
    }
    if (!response.ok || !body || !body.ok)
        throw new Error(body?.error || uconfigFormat("uconfig.error.http", "HTTP $1", response.status));
    return body;
}

async function uconfigRefresh() {
    const overall = document.getElementById("uconfig_overall");
    const consistency = document.getElementById("uconfig_consistency");
    overall && (overall.textContent = t("uconfig.status.verifying", "Verifying U-Config..."));
    uconfigSetMessage("", false);
    try {
        const response = await fetch("/uconfig/status", { method: "GET", cache: "no-store" });
        const status = await uconfigReadJson(response);
        overall && (overall.textContent = status.healthy ? t("uconfig.status.healthy", "Overall status: HEALTHY") :
            `${t("uconfig.status.attention", "Overall status: ATTENTION")} (${status.recommendation})`);
        uconfigRenderSlot("uconfig_active", t("uconfig.slot.active", "Active slot"), status.active);
        uconfigRenderSlot("uconfig_recovery", t("uconfig.slot.recovery", "Recovery slot"), status.recovery);
        if (consistency) {
            uconfigRenderRow("uconfig_consistency", t("uconfig.slot.consistency", "Cross-slot"), [
                [t("uconfig.fact.consistent", "Consistent"), status.copies_identical],
                [t("uconfig.fact.values", "Values"), true],
            ]);
        }
    } catch (error) {
        overall && (overall.textContent = t("uconfig.status.unavailable", "Overall status: UNAVAILABLE"));
        uconfigSetMessage(error?.message || String(error), true);
    }
}

async function uconfigProvision() {
    if (!confirm(t("uconfig.confirm.provision", "Provision or repair Keenetic U-Config? Existing valid identity values will be preserved.")))
        return;
    const form = new FormData();
    form.append("confirm", "PROVISION");
    uconfigSetMessage(t("uconfig.status.provisioning", "Provisioning / repairing. Do not power off..."), false);
    try {
        const response = await fetch("/uconfig/action", { method: "POST", body: form });
        const result = await uconfigReadJson(response);
        uconfigSetMessage(uconfigFormat("uconfig.status.provisioned", "Completed: $1. Values were not displayed.", result.action), false);
        await uconfigRefresh();
    } catch (error) {
        uconfigSetMessage(error?.message || String(error), true);
    }
}

async function uconfigBackup(slot) {
    const form = new FormData();
    form.append("slot", slot);
    const slotName = slot === "active" ? t("uconfig.slot.active", "Active slot") :
        t("uconfig.slot.recovery", "Recovery slot");
    uconfigSetMessage(uconfigFormat("uconfig.status.preparing_backup", "Preparing private $1 backup...", slotName), false);
    try {
        const response = await fetch("/uconfig/backup", { method: "POST", body: form });
        if (!response.ok) throw new Error(uconfigFormat("uconfig.error.backup_failed", "Backup failed (HTTP $1)", response.status));
        const blob = await response.blob();
        if (blob.size !== UCONFIG_IMAGE_SIZE)
            throw new Error(uconfigFormat("uconfig.error.backup_size", "Backup size mismatch: $1", blob.size));
        const disposition = response.headers.get("Content-Disposition");
        const filename = parseFilenameFromDisposition(disposition) ||
            (slot === "active" ? "NR3053_UConfig_active.bin" : "NR3053_UConfig_res.bin");
        const link = document.createElement("a");
        link.href = URL.createObjectURL(blob);
        link.download = filename;
        document.body.appendChild(link);
        link.click();
        document.body.removeChild(link);
        URL.revokeObjectURL(link.href);
        uconfigSetMessage(uconfigFormat("uconfig.status.backup_saved", "Private backup saved: $1", filename), false);
    } catch (error) {
        uconfigSetMessage(error?.message || String(error), true);
    }
}

async function uconfigRestore() {
    const input = document.getElementById("uconfig_file");
    const file = input?.files?.[0];
    if (!file) {
        uconfigSetMessage(t("uconfig.error.no_file", "Select a U-Config backup first."), true);
        return;
    }
    if (file.size !== UCONFIG_IMAGE_SIZE) {
        uconfigSetMessage(uconfigFormat("uconfig.error.invalid_size", "Invalid size: $1; expected 524288.", file.size), true);
        return;
    }
    if (!confirm(t("uconfig.confirm.restore", "Validate this device-specific backup and restore BOTH U-Config slots? Do not power off.")))
        return;
    const form = new FormData();
    form.append("confirm", "RESTORE");
    form.append("uconfig", file);
    uconfigSetMessage(t("uconfig.status.restoring", "Validating and restoring both slots. Do not power off..."), false);
    try {
        const response = await fetch("/uconfig/restore", { method: "POST", body: form });
        const result = await uconfigReadJson(response);
        uconfigSetMessage(uconfigFormat("uconfig.status.restored", "Completed: $1. Post-write verification passed.", result.action), false);
        await uconfigRefresh();
    } catch (error) {
        uconfigSetMessage(error?.message || String(error), true);
    }
}

function uconfigInit() {
    window.uconfigRefresh = uconfigRefresh;
    window.uconfigProvision = uconfigProvision;
    window.uconfigBackup = uconfigBackup;
    window.uconfigRestore = uconfigRestore;
    uconfigRefresh();
}
