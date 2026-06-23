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
const espirCluster = m.deviceAddCustomCluster(CLUSTER, {
    ID: 0xfc00,
    manufacturerCode: undefined, // private cluster, no manufacturer-specific framing
    attributes: {
        slotCount: {ID: 0x0000, type: Zcl.DataType.UINT8},
        activeLearnSlot: {ID: 0x0001, type: Zcl.DataType.UINT8},
        learnStatus: {ID: 0x0002, type: Zcl.DataType.ENUM8},
        lastSlot: {ID: 0x0003, type: Zcl.DataType.UINT8},
        lastCode: {ID: 0x0004, type: Zcl.DataType.OCTET_STR},
        lastKind: {ID: 0x0005, type: Zcl.DataType.ENUM8},
        fwRole: {ID: 0x0006, type: Zcl.DataType.ENUM8},
        lastCarrier: {ID: 0x0007, type: Zcl.DataType.UINT16},
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
    },
    commandsResponse: {},
});

const LEARN_STATUS = {0: 'idle', 1: 'waiting', 2: 'captured', 3: 'failed'};
const KIND = {0: 'raw', 1: 'nec'};
const ROLE = {0: 'master', 1: 'slave'};

// ---- fromZigbee: surface reported attributes -------------------------------
const fzEspir = {
    cluster: CLUSTER,
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg, publish, options, meta) => {
        const out = {};
        const a = msg.data;
        if (a.slotCount !== undefined) out.slot_count = a.slotCount;
        if (a.activeLearnSlot !== undefined) out.active_learn_slot = a.activeLearnSlot;
        if (a.learnStatus !== undefined) out.learn_status = LEARN_STATUS[a.learnStatus] ?? a.learnStatus;
        if (a.lastSlot !== undefined) out.last_slot = a.lastSlot;
        if (a.lastKind !== undefined) out.last_kind = KIND[a.lastKind] ?? a.lastKind;
        if (a.fwRole !== undefined) out.fw_role = ROLE[a.fwRole] ?? a.fwRole;
        if (a.lastCarrier !== undefined) out.last_carrier = a.lastCarrier;
        if (a.lastCode !== undefined) {
            const buf = Buffer.isBuffer(a.lastCode) ? a.lastCode : Buffer.from(a.lastCode);
            out.last_code = buf.toString('hex'); // HA replication reads this hex blob
        }
        return out;
    },
};

// ---- toZigbee: writing an expose issues the matching cluster command --------
// The custom cluster lives on endpoint 10, so always target that endpoint explicitly
// (Z2M would otherwise default to endpoint 1, which doesn't have cluster 0xFC00).
const ESPIR_EP = 10;
const espirEndpoint = (meta) => meta.device.getEndpoint(ESPIR_EP);

const cmd = (key, command) => ({
    key: [key],
    convertSet: async (entity, k, value, meta) => {
        await espirEndpoint(meta).command(CLUSTER, command, {slot: Number(value)}, {});
        return {state: {[key]: value}};
    },
});

const tzLearn = cmd('learn_slot', 'learn');
const tzSend = cmd('send_slot', 'send');
const tzClear = cmd('clear_slot', 'clear');

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

// ---- Definitions -----------------------------------------------------------
const baseExposes = (maxSlot) => [
    e.numeric('send_slot', ea.SET).withValueMin(0).withValueMax(maxSlot)
        .withDescription('Transmit the IR code stored in this slot'),
    e.enum('learn_status', ea.STATE, Object.values(LEARN_STATUS))
        .withDescription('State of the learn operation'),
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
    extend: [espirCluster],
    fromZigbee: [fzEspir],
    toZigbee: [tzLearn, tzSend, tzClear, tzProgram],
    exposes: [
        e.numeric('learn_slot', ea.SET).withValueMin(0).withValueMax(31)
            .withDescription('Enter learn mode for this slot, then press the remote key'),
        e.numeric('clear_slot', ea.SET).withValueMin(0).withValueMax(31)
            .withDescription('Erase the IR code in this slot'),
        ...baseExposes(31),
    ],
};

const slaveDefinition = {
    zigbeeModel: ['ESPIR-SLAVE'],
    model: 'ESPIR-SLAVE',
    vendor: 'ESPIR',
    description: 'ESP32-C6 Zigbee IR blaster — slave (transmit-only repeater)',
    extend: [espirCluster, m.battery()],
    fromZigbee: [fzEspir],
    toZigbee: [tzSend, tzClear, tzProgram],
    exposes: [
        e.numeric('clear_slot', ea.SET).withValueMin(0).withValueMax(31)
            .withDescription('Erase the IR code in this slot'),
        ...baseExposes(31),
    ],
};

module.exports = [masterDefinition, slaveDefinition];
