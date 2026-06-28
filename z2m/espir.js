/*
 * ESPIR — Zigbee2MQTT external converter.
 *
 * Mirrors components/espir_zcl/include/espir_proto.h. If you change cluster/attribute/
 * command IDs or the code-blob format there, change them here too.
 *
 * Install: copy this file into your Z2M `external_converters/` directory (often
 *   <z2m-data>/external_converters/espir.js), then restart Zigbee2MQTT.
 *
 * Targets modern Zigbee2MQTT (zigbee-herdsman-converters with modernExtend). If your Z2M
 * uses the older `module.exports = {...}` definition style or ESM `export default`, adapt
 * the export line at the bottom — the cluster/command/expose logic stays the same.
 */
const {Zcl} = require('zigbee-herdsman');
const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const tz = require('zigbee-herdsman-converters/converters/toZigbee');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const m = require('zigbee-herdsman-converters/lib/modernExtend');
const e = exposes.presets;
const ea = exposes.access;

const CLUSTER = 'espir';

// ---- Custom cluster 0xFC00 (mirror of espir_proto.h) -----------------------
const ESPIR_CLUSTER_DEF = {
    name: CLUSTER,            // REQUIRED: herdsman stores the def as-is; without a name,
                              // getCluster() resolves inbound 0xFC00 reports to 'undefined'.
    ID: 0xfc00,
    manufacturerCode: 0x1037, // must match ESPIR_MANUF_CODE in firmware (espir_proto.h)
    // NOTE: each attribute needs an explicit `name` — herdsman returns the attribute object
    // as-is and reads `.name` from it (it does NOT use the object key).
    attributes: {
        slotCount: {ID: 0x0000, type: Zcl.DataType.UINT8, name: 'slotCount'},
        activeLearnSlot: {ID: 0x0001, type: Zcl.DataType.UINT8, name: 'activeLearnSlot'},
        learnStatus: {ID: 0x0002, type: Zcl.DataType.ENUM8, name: 'learnStatus'},
        lastSlot: {ID: 0x0003, type: Zcl.DataType.UINT8, name: 'lastSlot'},
        selectedSlot: {ID: 0x0008, type: Zcl.DataType.UINT8, name: 'selectedSlot'},
        slotOccupied: {ID: 0x000a, type: Zcl.DataType.UINT8, name: 'slotOccupied'},
        lastCode: {ID: 0x0004, type: Zcl.DataType.OCTET_STR, name: 'lastCode'},
        lastKind: {ID: 0x0005, type: Zcl.DataType.ENUM8, name: 'lastKind'},
        fwRole: {ID: 0x0006, type: Zcl.DataType.ENUM8, name: 'fwRole'},
        lastCarrier: {ID: 0x0007, type: Zcl.DataType.UINT16, name: 'lastCarrier'},
    },
    commands: {
        learn: {ID: 0x00, parameters: [{name: 'slot', type: Zcl.DataType.UINT8}]},
        send: {ID: 0x01, parameters: [{name: 'slot', type: Zcl.DataType.UINT8}]},
        clear: {ID: 0x02, parameters: [{name: 'slot', type: Zcl.DataType.UINT8}]},
        program: {ID: 0x03, parameters: [
            {name: 'slot', type: Zcl.DataType.UINT8},
            {name: 'kind', type: Zcl.DataType.UINT8},
            {name: 'carrierKhz', type: Zcl.DataType.UINT16},
            {name: 'code', type: Zcl.DataType.OCTET_STR},
        ]},
        programBegin: {ID: 0x04, parameters: [
            {name: 'slot', type: Zcl.DataType.UINT8},
            {name: 'kind', type: Zcl.DataType.UINT8},
            {name: 'carrierKhz', type: Zcl.DataType.UINT16},
            {name: 'totalLen', type: Zcl.DataType.UINT16},
        ]},
        programChunk: {ID: 0x05, parameters: [
            {name: 'slot', type: Zcl.DataType.UINT8},
            {name: 'seq', type: Zcl.DataType.UINT8},
            {name: 'data', type: Zcl.DataType.OCTET_STR},
        ]},
        programCommit: {ID: 0x06, parameters: [{name: 'slot', type: Zcl.DataType.UINT8}]},
        copyTo: {ID: 0x07, parameters: [
            {name: 'slot', type: Zcl.DataType.UINT8},
            {name: 'ieee', type: Zcl.DataType.IEEE_ADDR},
        ]},
        copyAll: {ID: 0x08, parameters: [{name: 'ieee', type: Zcl.DataType.IEEE_ADDR}]},
        selectSlot: {ID: 0x09, parameters: [{name: 'slot', type: Zcl.DataType.UINT8}]},
    },
    commandsResponse: {},
};
const espirCluster = m.deviceAddCustomCluster(CLUSTER, ESPIR_CLUSTER_DEF);

const LEARN_STATUS = {0: 'idle', 1: 'waiting', 2: 'captured', 3: 'failed'};
const KIND = {0: 'raw', 1: 'nec'};
const ROLE = {0: 'master', 1: 'slave'};

// ---- fromZigbee: surface reported attributes -------------------------------
// Handle both name-resolved (a.slotCount) and raw numeric-ID (a['0']) attribute keys,
// because depending on how the custom cluster resolves, the inbound cluster may come in
// as the name 'espir' OR the numeric ID 0xFC00 — we register a converter for each.
const convertEspir = (msg) => {
    const a = msg.data || {};
    const g = (name, id) => (a[name] !== undefined ? a[name]
        : (a[id] !== undefined ? a[id] : a[String(id)]));
    const out = {};
    let v;
    if ((v = g('slotCount', 0)) !== undefined) out.slot_count = v;
    if ((v = g('learnStatus', 2)) !== undefined) out.learn_status = LEARN_STATUS[v] ?? v;
    if ((v = g('lastSlot', 3)) !== undefined) out.last_slot = v;
    if ((v = g('lastCode', 4)) !== undefined) {
        const buf = Buffer.isBuffer(v) ? v : Buffer.from(typeof v === 'string' ? v : (v.data || v));
        out.last_code = buf.toString('hex'); // HA replication reads this hex blob
    }
    if ((v = g('lastKind', 5)) !== undefined) out.last_kind = KIND[v] ?? v;
    if ((v = g('fwRole', 6)) !== undefined) out.fw_role = ROLE[v] ?? v;
    if ((v = g('lastCarrier', 7)) !== undefined) out.last_carrier = v;
    if ((v = g('selectedSlot', 8)) !== undefined) out.slot = v;
    if ((v = g('slotOccupied', 10)) !== undefined) out.slot_occupied = v ? 'stored' : 'empty';
    return out;
};

const mkFz = (cluster) => ({
    cluster,
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg, publish, options, meta) => convertEspir(msg),
});

const fzEspir = mkFz(CLUSTER);       // matches when the cluster resolves to the name 'espir'
const fzEspirRaw = mkFz(0xfc00);     // matches when it stays the numeric cluster id

// ---- toZigbee --------------------------------------------------------------
// The custom cluster lives on endpoint 10, so always target that endpoint explicitly
// (Z2M would otherwise default to endpoint 1, which doesn't have cluster 0xFC00).
const ESPIR_EP = 10;
const espirEndpoint = (meta) => meta.device.getEndpoint(ESPIR_EP);
const ATTRS = ['slotCount', 'fwRole', 'learnStatus', 'lastSlot', 'lastKind', 'lastCarrier', 'lastCode'];

// `slot` — the selector the action buttons act on. Sent to the device via the selectSlot
// command (0x09) so the master's OLED shows the live selection.
const tzSlot = {
    key: ['slot'],
    convertSet: async (entity, key, value, meta) => {
        const slot = Number(value);
        // Use a command, not an attribute write: manufacturer-specific attributes are not
        // OTA-writable on the device's zboss stack (it returns NOT_AUTHORIZED). The device
        // reports `selectedSlot` back, which fromZigbee syncs into state.slot.
        await espirEndpoint(meta).command(CLUSTER, 'selectSlot', {slot}, {});
        return {state: {slot}};
    },
};

// `action` — the Learn / Send / Clear buttons. Operates on the currently selected `slot`.
const tzAction = {
    key: ['action'],
    convertSet: async (entity, key, value, meta) => {
        const command = {learn: 'learn', send: 'send', clear: 'clear'}[value];
        if (!command) return;
        const slot = Number(meta.state?.slot ?? 0);
        await espirEndpoint(meta).command(CLUSTER, command, {slot}, {});
        return {state: {}};
    },
};

// Direct numeric commands for automations: publish e.g. {"send_slot": 5}. Not shown in the
// UI (the slot+action controls cover manual use), but handled here for scripts/dashboards.
const cmd = (key, command) => ({
    key: [key],
    convertSet: async (entity, k, value, meta) => {
        await espirEndpoint(meta).command(CLUSTER, command, {slot: Number(value)}, {});
        return {state: {}};
    },
});
const tzLearn = cmd('learn_slot', 'learn');
const tzSend = cmd('send_slot', 'send');
const tzClear = cmd('clear_slot', 'clear');

// Device-to-device replication: tell the MASTER to push a stored code straight to a slave
// (by IEEE) over Zigbee — handles long raw codes that don't fit the last_code attribute.
//   {"replicate_slot": {"slot": N, "target": "0x<slave-ieee>"}}
//   {"replicate_all": "0x<slave-ieee>"}   (push every non-empty slot)
const tzReplicate = {
    key: ['replicate_slot', 'replicate_all'],
    convertSet: async (entity, key, value, meta) => {
        const ep = espirEndpoint(meta);
        if (key === 'replicate_all') {
            await ep.command(CLUSTER, 'copyAll', {ieee: value}, {});
        } else {
            await ep.command(CLUSTER, 'copyTo', {slot: Number(value.slot), ieee: value.target}, {});
        }
        return {state: {}};
    },
};

// program_slot: {slot, kind: 'raw'|'nec', carrier_khz, code: '<hex>'} — used by HA
// replication to copy a learned code into a slave. Small codes go in one `program` frame;
// large raw codes are split into program_begin/chunk/commit (APS-fragmentable).
const tzProgram = {
    key: ['program_slot'],
    convertSet: async (entity, key, value, meta) => {
        const slot = Number(value.slot);
        const kind = value.kind === 'nec' ? 1 : 0;
        const carrier = Number(value.carrier_khz ?? 38);
        const code = Buffer.from(String(value.code), 'hex');
        const ep = espirEndpoint(meta);
        const MAX = 60; // conservative single-frame payload budget (bytes)
        if (code.length <= MAX) {
            await ep.command(CLUSTER, 'program', {slot, kind, carrierKhz: carrier, code}, {});
        } else {
            await ep.command(CLUSTER, 'programBegin', {slot, kind, carrierKhz: carrier, totalLen: code.length}, {});
            for (let seq = 0, off = 0; off < code.length; seq++, off += MAX) {
                await ep.command(CLUSTER, 'programChunk', {slot, seq, data: code.slice(off, off + MAX)}, {});
            }
            await ep.command(CLUSTER, 'programCommit', {slot}, {});
        }
        return {state: {}};
    },
};

// Register the custom cluster on the device (so inbound reports decode) and read the
// attributes once so values populate immediately, not just after the first report.
const espirConfigure = async (device, coordinatorEndpoint, definition) => {
    try { device.addCustomCluster(CLUSTER, ESPIR_CLUSTER_DEF); } catch (e) {}
    const ep = device.getEndpoint(ESPIR_EP);
    if (ep) {
        try { await ep.read(CLUSTER, ATTRS); } catch (e) { /* device asleep or busy */ }
    }
};

// ---- Definitions -----------------------------------------------------------
const slotExpose = e.numeric('slot', ea.STATE_SET).withValueMin(0).withValueMax(31)
    .withDescription('Slot the action buttons below operate on');

const statusExposes = [
    e.enum('slot_occupied', ea.STATE, ['empty', 'stored'])
        .withDescription('Whether the currently selected slot has a code stored'),
    e.enum('learn_status', ea.STATE, Object.values(LEARN_STATUS))
        .withDescription('State of the last learn operation'),
    e.numeric('last_slot', ea.STATE).withDescription('Slot most recently learned'),
    e.enum('last_kind', ea.STATE, Object.values(KIND)).withDescription('Encoding of the last learned code'),
    e.text('last_code', ea.STATE).withDescription('Last learned code as hex (used for slave replication)'),
    e.numeric('last_carrier', ea.STATE).withUnit('kHz').withDescription('Carrier frequency of the last learned code'),
    e.numeric('slot_count', ea.STATE).withDescription('Number of storage slots'),
];

const masterDefinition = {
    zigbeeModel: ['ESPIR-MASTER'],
    model: 'ESPIR-MASTER',
    vendor: 'ESPIR',
    description: 'ESP32-C6 Zigbee IR blaster — master (learn + store + transmit)',
    extend: [espirCluster, m.ota()],
    fromZigbee: [fzEspir, fzEspirRaw],
    toZigbee: [tzSlot, tzAction, tzLearn, tzSend, tzClear, tzProgram, tzReplicate],
    configure: espirConfigure,
    exposes: [
        slotExpose,
        e.enum('action', ea.SET, ['learn', 'send', 'clear'])
            .withDescription('Learn into / transmit / clear the selected slot. ' +
                             'Set "slot" first, then pick an action.'),
        ...statusExposes,
    ],
};

const slaveDefinition = {
    zigbeeModel: ['ESPIR-SLAVE'],
    model: 'ESPIR-SLAVE',
    vendor: 'ESPIR',
    description: 'ESP32-C6 Zigbee IR blaster — slave (transmit-only repeater)',
    extend: [espirCluster],
    fromZigbee: [fzEspir, fzEspirRaw, fz.battery],
    toZigbee: [tzSlot, tzAction, tzSend, tzClear, tzProgram],
    configure: espirConfigure,
    exposes: [
        slotExpose,
        e.enum('action', ea.SET, ['send', 'clear'])
            .withDescription('Transmit / clear the selected slot. Set "slot" first.'),
        e.text('last_code', ea.STATE).withDescription('Last code programmed (hex)'),
        e.numeric('slot_count', ea.STATE).withDescription('Number of storage slots'),
        e.battery(),            // LiPo % from the Power Config cluster (device self-reports)
    ],
};

module.exports = [masterDefinition, slaveDefinition];
