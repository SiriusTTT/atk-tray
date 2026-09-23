const HID = require('T:/Qoder_WorkSpace/atk-hub-extracted/node_modules/node-hid');

const VID = 0x373B;
const PID = 0x101B;

const devices = HID.devices().filter(d => d.vendorId === VID && d.productId === PID);

const vendorCols = ['Col01', 'Col02', 'Col05', 'Col06', 'Col07'];
const allCols = ['Col01', 'Col02', 'Col03', 'Col04', 'Col05', 'Col06', 'Col07', 'Col08'];

const openedDevices = {};

for (const dev of devices) {
  for (const col of allCols) {
    if (dev.path.includes(col)) {
      try {
        const h = new HID.HID(dev.path);
        const key = `${col}(mi_${String(dev.interface).padStart(2,'0')})`;
        openedDevices[key] = h;
        console.log(`Opened ${key} usagePage=${dev.usagePage} usage=${dev.usage}`);

        h.on('data', (data) => {
          const hex = Buffer.from(data).toString('hex');
          console.log(`[${key}] DATA: ${hex}`);
        });

        h.on('error', (err) => {
          console.log(`[${key}] ERROR: ${err.message}`);
        });
      } catch(e) {
        console.log(`  Failed to open ${col}: ${e.message}`);
      }
    }
  }
}

console.log(`\n=== Monitoring ${Object.keys(openedDevices).length} collections ===`);
console.log('Press DPI button, move mouse, click buttons...');
console.log('Recording for 30 seconds...\n');

// After 3 seconds, send some test commands on col05
setTimeout(() => {
  const col05 = openedDevices['Col05(mi_01)'];
  if (col05) {
    console.log('=== Sending test commands on Col05 ===');

    // Try 08 XX sub-commands
    for (let sub = 0; sub <= 0x1F; sub++) {
      const buf = Buffer.alloc(17);
      buf[0] = 0x08; // report ID
      buf[1] = sub;
      try {
        col05.write(Array.from(buf));
      } catch(e) {}
    }
    console.log('Sent 08 00 through 08 1F');

    // Try longer commands with 08 prefix
    setTimeout(() => {
      // Try 08 03 (possible battery query)
      for (let len = 2; len <= 17; len++) {
        const buf = Buffer.alloc(17);
        buf[0] = 0x08;
        buf[1] = 0x03;
        buf[2] = len - 2; // length prefix?
        try {
          col05.write(Array.from(buf));
        } catch(e) {}
      }
      console.log('Sent 08 03 with varying lengths');
    }, 2000);
  }
}, 3000);

// Also try sending on col07
setTimeout(() => {
  const col07 = openedDevices['Col07(mi_01)'];
  if (col07) {
    console.log('\n=== Sending test commands on Col07 ===');
    for (let cmd = 0; cmd <= 0x0F; cmd++) {
      const buf = Buffer.alloc(49);
      buf[0] = cmd;
      try {
        col07.write(Array.from(buf));
      } catch(e) {}
    }
    console.log('Sent single-byte commands 00-0F on Col07');
  }
}, 6000);

setTimeout(() => {
  console.log('\n=== Done ===');
  for (const [key, h] of Object.entries(openedDevices)) {
    try { h.close(); } catch(e) {}
  }
  process.exit(0);
}, 30000);
