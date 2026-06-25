// Asserts z2m/espir.js mirrors the attribute IDs in espir_proto.h.
const fs = require('fs');
const assert = require('assert');

const proto = fs.readFileSync(__dirname + '/../components/espir_zcl/include/espir_proto.h', 'utf8');
const js = fs.readFileSync(__dirname + '/espir.js', 'utf8');

function protoAttr(name) {
  const m = proto.match(new RegExp(name + '\\s+(0x[0-9A-Fa-f]+)'));
  return m ? parseInt(m[1], 16) : undefined;
}
function jsAttr(name) {
  const m = js.match(new RegExp(name + ':\\s*\\{ID:\\s*(0x[0-9A-Fa-f]+)'));
  return m ? parseInt(m[1], 16) : undefined;
}

const selected = protoAttr('ESPIR_ATTR_SELECTED_SLOT');
assert.strictEqual(selected, 0x0008, 'proto SELECTED_SLOT must be 0x0008');
assert.strictEqual(jsAttr('selectedSlot'), selected, 'espir.js selectedSlot must mirror proto');

const occupied = protoAttr('ESPIR_ATTR_SLOT_OCCUPIED');
assert.strictEqual(occupied, 0x000A, 'proto SLOT_OCCUPIED must be 0x000A');
assert.strictEqual(jsAttr('slotOccupied'), occupied, 'espir.js slotOccupied must mirror proto');

console.log('parity OK: selectedSlot = 0x' + selected.toString(16) +
            ', slotOccupied = 0x' + occupied.toString(16));
