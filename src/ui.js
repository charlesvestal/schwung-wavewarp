import {
    MoveMainKnob, MoveMainButton,
    MoveUp, MoveDown, MoveBack,
    MoveKnob1, MoveKnob2, MoveKnob3, MoveKnob4,
    MoveKnob5, MoveKnob6, MoveKnob7, MoveKnob8
} from '/data/UserData/schwung/shared/constants.mjs';

import { isCapacitiveTouchMessage, decodeDelta } from '/data/UserData/schwung/shared/input_filter.mjs';

const ENCODER_CCS = [MoveKnob1, MoveKnob2, MoveKnob3, MoveKnob4, MoveKnob5, MoveKnob6, MoveKnob7, MoveKnob8];
const SCREEN_WIDTH = 128;
const ROW_HEIGHT = 10;

let hierarchy = null;
let levels = {};
let chainParams = [];
let chainParamsMap = {};
let currentLevelName = 'root';
let currentLevel = null;
let menuIndex = 0;
let needsRedraw = true;
let listIndex = 0;
let listCount = 0;
let listName = '';
let listBankName = 'WaveWarp';
let listPatchNumber = 1;
let listItems = [];

function init() {
    loadMetadata();
    navigateTo('root');
}

function loadMetadata() {
    const hierarchyJson = host_module_get_param('ui_hierarchy');
    const chainParamsJson = host_module_get_param('chain_params');

    if (hierarchyJson) {
        hierarchy = JSON.parse(hierarchyJson);
        levels = hierarchy.levels || {};
    }

    if (chainParamsJson) {
        chainParams = JSON.parse(chainParamsJson);
        chainParamsMap = {};
        for (const param of chainParams)
            chainParamsMap[param.key] = param;
    }
}

function navigateTo(levelName) {
    if (!levels[levelName])
        return;
    currentLevelName = levelName;
    currentLevel = levels[levelName];
    menuIndex = 0;
    refreshListState();
    needsRedraw = true;
}

function refreshListState() {
    if (!currentLevel || !currentLevel.list_param) {
        listIndex = 0;
        listCount = 0;
        listName = '';
        listBankName = 'WaveWarp';
        listPatchNumber = 1;
        listItems = [];
        return;
    }

    listIndex = parseInt(host_module_get_param(currentLevel.list_param) || '0', 10) || 0;
    listCount = parseInt(host_module_get_param(currentLevel.count_param) || '0', 10) || 0;
    listName = host_module_get_param(currentLevel.name_param) || '';
    listBankName = currentLevel.bank_name_param ? (host_module_get_param(currentLevel.bank_name_param) || 'WaveWarp') : 'WaveWarp';
    listPatchNumber = currentLevel.patch_param ? (parseInt(host_module_get_param(currentLevel.patch_param) || '1', 10) || 1) : (listIndex + 1);
    listItems = [];
    if (currentLevel.items_param) {
        const itemsJson = host_module_get_param(currentLevel.items_param);
        if (itemsJson) {
            try {
                listItems = JSON.parse(itemsJson);
            } catch (e) {
                listItems = [];
            }
        }
    }
}

function currentItems() {
    if (!currentLevel)
        return [];
    return currentLevel.params || [];
}

function itemKey(item) {
    if (typeof item === 'string')
        return item;
    if (item && item.key)
        return item.key;
    return null;
}

function itemLabel(item) {
    if (typeof item === 'string')
        return (chainParamsMap[item] && chainParamsMap[item].name) || item;
    if (item && item.label)
        return item.label;
    if (item && item.key)
        return (chainParamsMap[item.key] && chainParamsMap[item.key].name) || item.key;
    return '---';
}

function getMeta(key) {
    return chainParamsMap[key] || { key, name: key, type: 'float', min: 0, max: 1, step: 0.02, default: 0 };
}

function getValue(key) {
    const meta = getMeta(key);
    const raw = host_module_get_param(key);
    const fallback = meta.default !== undefined ? meta.default : 0;
    if (meta.type === 'float')
        return parseFloat(raw !== undefined && raw !== null && raw !== '' ? raw : fallback) || 0;
    return parseInt(raw !== undefined && raw !== null && raw !== '' ? raw : fallback, 10) || 0;
}

function setValue(key, value) {
    const meta = getMeta(key);
    let next = value;

    if (meta.min !== undefined && next < meta.min)
        next = meta.min;
    if (meta.max !== undefined && next > meta.max)
        next = meta.max;

    if (meta.type === 'int' || meta.type === 'enum')
        next = Math.round(next);

    const valStr = meta.type === 'float' ? next.toFixed(6) : String(next);
    host_module_set_param(key, valStr);
    needsRedraw = true;
}

function formatValue(key) {
    const meta = getMeta(key);
    const value = getValue(key);

    if (meta.type === 'enum' && meta.options)
        return meta.options[value] || String(value);

    if (meta.type === 'int')
        return String(Math.round(value));

    if (Math.abs(value) >= 100 || (meta.step && meta.step >= 1))
        return `${Math.round(value)}${meta.unit || ''}`;

    return `${value.toFixed(2)}${meta.unit || ''}`;
}

function adjustValue(key, delta) {
    const meta = getMeta(key);
    const step = meta.step !== undefined ? meta.step : (meta.type === 'float' ? 0.02 : 1);
    setValue(key, getValue(key) + delta * step);
}

function handleMenuMove(delta) {
    const items = currentItems();
    if (!items.length)
        return;
    menuIndex += delta;
    if (menuIndex < 0)
        menuIndex = 0;
    if (menuIndex >= items.length)
        menuIndex = items.length - 1;
    needsRedraw = true;
}

function activateCurrentItem() {
    const item = currentItems()[menuIndex];
    if (!item)
        return;
    if (typeof item === 'object' && item.level) {
        navigateTo(item.level);
        return;
    }
    if (currentLevelName === 'root') {
        const firstSublevel = currentItems().find(entry => typeof entry === 'object' && entry.level && entry.level !== 'presets');
        if (firstSublevel && firstSublevel.level)
            navigateTo(firstSublevel.level);
    }
}

function handleListMove(delta) {
    if (!currentLevel || !currentLevel.list_param || !delta)
        return false;

    refreshListState();
    const maxIndex = Math.max(0, listCount - 1);
    let nextIndex = listIndex + delta;
    if (nextIndex < 0)
        nextIndex = 0;
    if (nextIndex > maxIndex)
        nextIndex = maxIndex;
    if (nextIndex === listIndex) {
        needsRedraw = true;
        return true;
    }

    host_module_set_param(currentLevel.list_param, String(nextIndex));
    refreshListState();
    needsRedraw = true;
    return true;
}

function handleKnob(cc, value) {
    if (currentLevel && currentLevel.list_param) {
        const knobIndex = ENCODER_CCS.indexOf(cc);
        if (knobIndex < 0)
            return false;

        const delta = decodeDelta(value);
        if (!delta)
            return true;

        handleListMove(delta);
        return true;
    }

    if (!currentLevel || !currentLevel.knobs)
        return false;

    const knobIndex = ENCODER_CCS.indexOf(cc);
    if (knobIndex < 0 || knobIndex >= currentLevel.knobs.length)
        return false;

    const delta = decodeDelta(value);
    if (!delta)
        return true;

    const key = currentLevel.knobs[knobIndex];
    if (!key)
        return true;

    adjustValue(key, delta);
    return true;
}

function onMidiMessageInternal(msg) {
    if (!msg || msg.length < 3)
        return;
    handleMidiMessage(msg);
}

function onMidiMessageExternal(msg) {
    if (!msg || msg.length < 3)
        return;
    handleMidiMessage(msg);
}

function handleMidiMessage(msg) {
    if (isCapacitiveTouchMessage(msg))
        return;

    const status = msg[0] & 0xF0;
    const cc = msg[1];
    const value = msg[2];

    if (status !== 0xB0)
        return;

    if (handleKnob(cc, value))
        return;

    if (currentLevel && currentLevel.list_param) {
        if (cc === MoveMainKnob) {
            const delta = decodeDelta(value);
            if (delta)
                handleListMove(delta);
            return;
        }

        if (value === 0)
            return;

        if (cc === MoveUp) {
            handleListMove(-1);
        } else if (cc === MoveDown) {
            handleListMove(1);
        } else if (cc === MoveMainButton || cc === MoveBack) {
            navigateTo('root');
        }
        return;
    }

    if (cc === MoveMainKnob) {
        const delta = decodeDelta(value);
        if (delta)
            handleMenuMove(delta);
        return;
    }

    if (value === 0)
        return;

    if (cc === MoveUp) {
        handleMenuMove(-1);
    } else if (cc === MoveDown) {
        handleMenuMove(1);
    } else if (cc === MoveMainButton) {
        activateCurrentItem();
    } else if (cc === MoveBack) {
        if (currentLevelName !== 'root')
            navigateTo('root');
    }
}

function draw() {
    display.clear();

    if (!currentLevel) {
        display.drawText(2, 2, 'WaveWarp', 1);
        display.drawText(2, 14, 'Loading...', 1);
        display.flush();
        return;
    }

    const title = currentLevel.label || currentLevel.name || 'WaveWarp';
    display.drawText(2, 2, title, 1);

    if (currentLevel.list_param) {
        refreshListState();
        display.drawText(2, 12, listBankName.slice(0, 18), 1);
        display.drawText(92, 12, (`#${listPatchNumber}`).slice(0, 6), 1);

        const rows = listItems.length ? listItems : [{ index: listIndex, name: listName || '---' }];
        const start = Math.max(0, Math.min(listIndex - 2, Math.max(0, rows.length - 4)));
        for (let i = 0; i < 4 && start + i < rows.length; i++) {
            const item = rows[start + i];
            const y = 22 + i * ROW_HEIGHT;
            const selected = start + i === listIndex;
            const label = `${item.index + 1} ${item.name || '---'}`;

            if (selected)
                display.fillRect(0, y - 1, SCREEN_WIDTH, ROW_HEIGHT, 1);

            display.drawText(2, y, label.slice(0, 20), selected ? 0 : 1);
        }

        display.drawText(2, 56, (currentLevel.hint || 'Jog: browse').slice(0, 21), 1);
        display.flush();
        return;
    }

    const items = currentItems();
    const start = Math.max(0, Math.min(menuIndex - 2, Math.max(0, items.length - 5)));
    for (let i = 0; i < 5 && start + i < items.length; i++) {
        const item = items[start + i];
        const y = 14 + i * ROW_HEIGHT;
        const selected = start + i === menuIndex;
        const label = itemLabel(item);
        const key = itemKey(item);
        const value = key ? formatValue(key) : '>';

        if (selected)
            display.fillRect(0, y - 1, SCREEN_WIDTH, ROW_HEIGHT, 1);

        display.drawText(2, y, label.slice(0, 14), selected ? 0 : 1);
        display.drawText(82, y, value.slice(0, 7), selected ? 0 : 1);
    }

    if (currentLevel.knobs && currentLevel.knobs.length) {
        display.drawText(2, 56, 'K1-K8: page params', 1);
    } else {
        display.drawText(2, 56, 'Press: open/back', 1);
    }
    display.flush();
}

function tick() {
    if (!needsRedraw)
        return;
    draw();
    needsRedraw = false;
}

globalThis.init = init;
globalThis.tick = tick;
globalThis.onMidiMessageInternal = onMidiMessageInternal;
globalThis.onMidiMessageExternal = onMidiMessageExternal;
