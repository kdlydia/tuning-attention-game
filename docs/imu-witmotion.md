# The WitMotion IMU in MayaFlux

How we got a WitMotion WT901BLE (accelerometer, gyroscope and angles) into a MayaFlux program, and what we learned along the way. Tested on 25 Nov 2026 with Tunnel Colour.

## The short version

1. The IMU talks Bluetooth Low Energy, and MayaFlux has no Bluetooth input. So a small bridge script reads the IMU and sends its values as OSC.
2. The MayaFlux program reads that OSC on a UDP port of its own.

```
WT901BLE --BLE--> tools/witmotion_bridge --OSC over UDP 8000--> MayaFlux program
```

## Running the bridge

Once:

```sh
cd tools/witmotion_bridge
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

Each time, with the IMU switched on:

```sh
.venv/bin/python witmotion_bridge.py --rate 50
```

It finds the IMU by name (`WT901...`), connects, and sends three OSC messages per reading to 127.0.0.1:8000:

| Address | Values |
|---|---|
| `/imu/acc` | acceleration x y z, in g |
| `/imu/gyro` | rotation speed x y z, in degrees per second |
| `/imu/angle` | roll, pitch, yaw, in degrees |

Options:
- `--osc host:port` sends somewhere else.
- `--no-osc` stops the OSC.
- `--print` shows the values as they come.
- `--pty PATH` also copies the raw packets into a virtual serial port. See "Other ways in".

Ctrl-C disconnects from the IMU and stops the bridge.

## Reading it in MayaFlux

Turn on the UDP backend in the project's `mayaflux.json`:

```json
{ "network": { "udp": { "enabled": true } } }
```

Then open a UDP endpoint and handle the messages:

```cpp
static auto& imu = *new std::shared_ptr<Vruta::NetworkSource>(std::make_shared<Vruta::NetworkSource>(
    Core::EndpointInfo { .transport = Core::NetworkTransport::UDP, .role = Core::EndpointRole::RECEIVE,
                         .label = "imu", .local_port = 8000 }));

on_message_received(imu, [](const Core::NetworkMessage& message) {
    const auto osc = Portal::Network::as_osc(message);
    if (osc && osc->address == "/imu/angle") {
        float roll = osc->get_float(0).value_or(0.0F);
        // store in an atomic: this runs on the network thread
    }
});
```

The handler runs on MayaFlux's network thread. Hand values to the rest of the program through atomics or a lock, as we do for the pen.

## Why not MayaFlux's own OSC input

MayaFlux has an OSC input: `input.osc.enabled` in `mayaflux.json`, then `vega.read_osc(...)`. It needs `network.udp.enabled` as well. It works on its own, but it crashed Tunnel Colour within a couple of minutes, because of a MayaFlux bug:

- **Input values from different devices share one queue that only one thread may push into.** Every input backend pushes into one queue, a `LockFreeQueue`, which is single-producer only. The tablet and OSC each push from their own thread, so the queue gets corrupted. In our case the input thread read a broken OSC message and the program aborted with `std::bad_alloc`. Any two input devices at once, tablet and OSC or HID and MIDI, can trigger this.
- **An input node's last-input and history stop updating after 63 values.** The history is a fixed queue that nothing empties. The node's value and its callbacks (`on_input`, `on_message` and so on) keep working, so use those.

Both are in the current MayaFlux main and develop branches. We plan to report them. Until they're fixed, reading OSC on our own endpoint keeps the IMU off that shared queue.

## Things to know about the IMU

- **Data:** it sends 20-byte packets, `55 61` followed by nine 16-bit values. The bridge decodes them.
- **Rate:** by default it sends 10 readings a second. `--rate 50` asks for 50 for this session only, without saving it to the device, and we get about 25 a second over Bluetooth.
- **Stuck link:** while something is connected to it, it stops advertising. If a bridge dies without disconnecting, the next one can't find it. Fix: `bluetoothctl disconnect <address>` (ours is `D5:D8:6C:F8:18:42`).
- **Only one bridge at a time:** an old bridge left running keeps the IMU to itself.

## Other ways in

- **As a serial port:** `--pty PATH` writes the raw packets into a virtual serial port. MayaFlux can open it with `open_serial_port` and decode the packets itself, with a custom parser (`SerialSpec::raw().parse_with(...)`). This would also go through the shared input queue, so it has the same multi-device bug.
- **Directly from the program:** a Bluetooth reader inside the program could push values into MayaFlux's input system. It's more work, and the queue bug applies there too.

## How MayaFlux input fits together

- **The network subsystem** owns sockets and serial ports on one network thread: UDP, TCP, local sockets, serial.
- **The input subsystem** owns the devices (HID, MIDI, tablet, serial, OSC). It turns their data into input values, which go through one queue to one input thread. That thread calls the input nodes bound to each device.
- **Input nodes** each follow one number, smoothed, and fire callbacks on the input thread.
- **`Vruta::NetworkSource`** delivers network messages to coroutines or handlers without going through the input system. That's why it avoids the queue bug.
