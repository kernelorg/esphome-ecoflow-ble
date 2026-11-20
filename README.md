**English** | [Русский](README.ru.md) | [Українська](README.uk.md) | [Polski](README.pl.md)

# esphome-ecoflow-ble

ESPHome external components for local control of **EcoFlow portable power
stations** over Bluetooth LE from an ESP32 (esp-idf). After a one-time setup
the device works without cloud, internet or Wi-Fi.

| Component | Purpose |
|---|---|
| `ecoflow_ble` | One block per power station: finds it by its serial number, detects the model and the encryption, authenticates and switches outputs (AC / USB / DC) and the built-in light, reads battery, power and output states |
| `ef_provision` | One-time setup: gets Wi-Fi from the phone over **Improv BLE**, serves a setup page, logs in to the EcoFlow cloud once, lets the user tick the units found nearby and stores everything in NVS |
| `ef_nvs.h` (inside `ef_provision`) | Helpers `ef_nvs::get_str/set_str/get_u8/set_u8/get_bool/set_bool/want_provisioning()` for the `ecoflow` NVS namespace, callable from YAML lambdas |
| [`ui/lvgl_debug.yaml`](ui/lvgl_debug.yaml) | Optional on-device debug UI (LVGL + display + two buttons) |

## Before you start

- **The power stations must already be added to your EcoFlow account** in the
  official EcoFlow app, and all of them to the **same** account. The ESP32
  authenticates with that account's `user_id`; a unit that is not bound to it
  is rejected ("device must be set up in the EcoFlow app first" / "device
  already bound to another account").
- **Keep the ESP32 close to the power stations.** Bluetooth LE reaches a few
  metres, best in the same room; walls and the stations' metal cases shorten
  the range. The ESP32 must stay within range during setup (to find the units)
  and in operation (to control them).
- The ESP32 only supports **2.4 GHz** Wi-Fi; Wi-Fi is needed only during setup.

## Supported models

The model is recognised by the first characters of the serial number; the
encryption type (0, 1 or 7) is read from the unit's advertisement.

| Family | Models (serial prefix) | Command protocol | Outputs |
|---|---|---|---|
| Delta 3 | Delta 3 (P231), Delta 3 Plus (P351), Delta 3 Classic (P321), Delta 3 Air (PR11/PR12/PR21), Delta 3 Max (D3N1), Delta 3 Ultra (D751) | protobuf `ConfigWrite` | `ac`, `usb`¹, `dc`¹ |
| Delta 3 (two AC banks) | Delta 3 Max Plus (D3M1), Delta 3 Ultra Plus (D511) | protobuf `ConfigWrite` | `ac`, `ac2`, `usb`¹, `dc`¹ |
| River 3 | River 3 (R651/R653/R654/R655) | protobuf `ConfigWrite` | `ac`, `dc` |
| River 3 Plus | River 3 Plus (R631/R634/R635) | protobuf `ConfigWrite` | `ac`, `dc`, light (off / dim / bright / SOS) |
| Delta Pro 3 | Delta Pro 3 (MR51/MR54) | protobuf `ConfigWrite` | `ac` (= both banks), `ac_hv`, `ac_lv`, `dc`, `usb`¹ |
| Delta 2 | Delta 2 (R331/R335), Delta 2 Max (R351/R354), Delta 3 1500 (D361) | per-output commands, packet v2 | `ac`, `usb`, `dc` |
| River 2 | River 2 (R601/R603), River 2 Max (R611/R613), River 2 Pro (R621/R623) | per-output commands, packet v2 | `ac`, `dc` |

USB cannot be switched on the River 2 and River 3 families.

Tested on hardware:
- **River 3 Plus** ×2 — setup over Improv, cloud login, picking units, authentication, DC, light, two units at once;
- **Delta 3 Plus** — authentication.

Everything else follows the protocol as implemented in
[rabits/ha-ef-ble](https://github.com/rabits/ha-ef-ble) and has not been
tested yet.

¹ The field exists in the model's `ConfigWrite` protobuf but is not switched by
ha-ef-ble — treat it as experimental.

Other EcoFlow devices (Delta Pro, Delta Max, River Max, PowerStream, Smart
Generator, Wave, …) are reported as `Unknown (XXXX)` and get no controls.

## Setup with a phone (Improv BLE)

On first boot (no stored credentials), or after *Start setup*, the device
enters **setup mode**: Bluetooth announces it as an Improv device and the
previously stored Wi-Fi network is forgotten. The phone sends the Wi-Fi
network to use with an **Improv client**:

- **Android / Windows / macOS / Linux:** https://www.improv-wifi.com in
  Chrome or Edge (Web Bluetooth) → *Connect* → choose the device.
- **iPhone / iPad:** Safari and Chrome on iOS have no Web Bluetooth; open
  https://www.improv-wifi.com in a Web Bluetooth browser such as **Bluefy**.
- **Home Assistant** with Bluetooth near the device discovers it through the
  *Improv via BLE* integration and asks for the Wi-Fi network.

The Wi-Fi network must have **internet access** (needed once, for the EcoFlow
login) and be **2.4 GHz**. Two ways:

### Option 1 — the phone shares its own hotspot

Use this away from home (no router needed); the login goes through the phone's
mobile data.

1. Turn on the phone's hotspot and make it 2.4 GHz:
   - Android: *Hotspot → Band → 2.4 GHz*;
   - iPhone: *Personal Hotspot → Maximize Compatibility* on.
2. Keep the hotspot on and open the Improv client on the **same phone**.
3. Connect to the device and send the **hotspot's** name and password.
4. The ESP32 joins the hotspot; the Improv client offers *Visit device*,
   which opens `http://<ip>/` (the phone is the router, so it reaches the
   ESP32 directly).

### Option 2 — an existing Wi-Fi network with internet

Use this at home: the phone and the ESP32 both join the router.

1. Connect the phone to the Wi-Fi network (a 2.4 GHz network, or a dual-band
   router that also broadcasts 2.4 GHz).
2. Open the Improv client, connect to the device and send **that network's**
   name and password.
3. The ESP32 joins the same network; *Visit device* opens `http://<ip>/`.
   The phone must stay on this network (not on mobile data) to open the page.

### Setup page (both options)

1. **Log in** with the EcoFlow account e-mail and password. The ESP32 sends
   them to `https://api.ecoflow.com/auth/login` (falls back to `api-e` /
   `api-a`, TLS checked against the built-in CA bundle) and keeps only the
   `user_id`; the password is not stored.
2. **Choose the units.** The page lists the EcoFlow units heard over Bluetooth
   nearby (model, serial, signal), strongest first. Tick up to as many as the
   firmware has `ecoflow_ble` blocks, or type a serial by hand. *Refresh list*
   if a unit is missing (is it on and close?).
3. **Save**, then *Reboot now* (or *Exit setup* on the device). The device
   restarts in normal mode: Wi-Fi off, the units are found over BLE and
   authenticated.

To change the units or the account later, enter setup again (*Start setup* in
the debug UI, or an `ef_provision` button with `type: start_setup`, see
[Entities](#entities)).

## Configuration

```yaml
external_components:
  - source: github://kernelorg/esphome-ecoflow-ble@main
    components:
      - ecoflow_ble
      - ef_provision
  # or a local copy:
  # - source:
  #     type: local
  #     path: <path>/esphome-ecoflow-ble/components
  #   components:
  #     - ecoflow_ble
  #     - ef_provision

esp32:
  board: esp32dev
  framework:
    type: esp-idf

esp32_ble:
  enable_on_boot: false        # ef_provision enables it at boot (both modes)

wifi:
  enable_on_boot: false        # Wi-Fi only in setup mode; ef_provision enables it

improv_ble:                    # the phone sends the Wi-Fi network over BLE
  authorizer: none
  next_url: "http://{{ip_address}}/"   # "Visit device" opens the setup page

esp32_ble_tracker:
  scan_parameters:
    active: true
    continuous: false

ef_provision:
  id: ef_prov
  ecoflow_ble_id: unit1        # list the units heard nearby on the setup page
  # forget_wifi_on_setup: true # forget the stored Wi-Fi when setup starts
  # auto_start: true           # pick the mode at boot, enable BLE, scan

ble_client:                    # one per power station
  - id: ef_client1
    mac_address: 00:00:00:00:00:00
    auto_connect: true
  - id: ef_client2
    mac_address: 00:00:00:00:00:00
    auto_connect: true

ecoflow_ble:                   # one per power station
  - id: unit1
    ble_client_id: ef_client1
    slot: 0                    # 1st unit chosen on the setup page
  - id: unit2
    ble_client_id: ef_client2
    slot: 1                    # 2nd unit chosen
```

### `ecoflow_ble` options

| Option | Default | |
|---|---|---|
| `ble_client_id` | — | the `ble_client` used for this unit (its MAC is set automatically when the unit is found) |
| `slot` | — | load the serial of setup slot N (0 = first unit chosen) and the shared `user_id` from NVS |
| `device_sn`, `user_id` | `""` | fixed credentials instead of `slot` (no setup page needed) |
| `auto_connect` | `true` | connect as soon as the unit's advertisement is seen |

### `ef_provision` options

| Option | Default | |
|---|---|---|
| `ecoflow_ble_id` | — | any `ecoflow_ble`: the setup page then lists the units heard nearby |
| `forget_wifi_on_setup` | `true` | erase the stored Wi-Fi when setup mode starts, so Improv always asks |
| `auto_start` | `true` | at boot: setup mode when nothing is stored or setup was requested, normal mode otherwise; enables BLE and starts the scan — no `on_boot` needed |

The number of units that can be chosen equals the number of `ecoflow_ble`
blocks (up to 8). ESP32 keeps 3 BLE connections by default (`esp32_ble:
max_connections`, up to 9) and every connection costs heap — 2–3 units are a
realistic maximum next to Wi-Fi and a display.

## Using it from your own configuration

### Entities

The usual ESPHome way — for automations, the web page or Home Assistant:

```yaml
switch:
  - platform: ecoflow_ble
    ecoflow_ble_id: unit1
    id: unit1_ac
    output: ac                   # ac | ac2 | ac_hv | ac_lv | usb | dc
    name: "AC output"
select:
  - platform: ecoflow_ble
    ecoflow_ble_id: unit1
    name: "Light"                # Off / Dim / Bright / SOS (River 3 Plus)
sensor:
  - platform: ecoflow_ble
    ecoflow_ble_id: unit1
    type: battery_level          # see the table below
    name: "Battery"
  - platform: ecoflow_ble
    ecoflow_ble_id: unit1
    type: output_power
    name: "Output power"
binary_sensor:
  - platform: ecoflow_ble
    ecoflow_ble_id: unit1
    type: authenticated          # found | connected | authenticated
    name: "EcoFlow ready"
text_sensor:
  - platform: ecoflow_ble
    ecoflow_ble_id: unit1
    type: state                  # model | serial | state | error | address | outputs
    name: "EcoFlow state"
  - platform: ef_provision
    type: status                 # status | ip
    name: "Setup status"
button:
  - platform: ef_provision
    id: start_setup
    type: start_setup            # start_setup | exit_setup | forget_wifi
    name: "Start setup"
```

| Platform | Of | `type` / option | |
|---|---|---|---|
| `switch` | `ecoflow_ble` | `output:` `ac`, `ac2`, `ac_hv`, `ac_lv`, `usb`, `dc` | switches the output; the state is the real one from the unit's telemetry (Delta 3 / River 3 / Delta Pro 3). Until the unit has reported the output — and on Delta 2 / River 2 — it is the last command sent (assumed state). Nothing is switched at boot |
| `select` | `ecoflow_ble` | — | built-in light: Off / Dim / Bright / SOS |
| `sensor` | `ecoflow_ble` | `rssi` (default), `battery_level`, `input_power`, `output_power`, `ac_input_power`, `ac_output_power`, `dc_output_power`, `usb_c_output_power`, `usb_a_output_power`, `solar_input_power`, `charge_remaining`, `discharge_remaining`, `cell_temperature`, `charge_limit`, `discharge_limit` | RSSI (dBm) works for every model; the rest come from the telemetry: battery %, power W (outputs as positive values), remaining charge / discharge time in minutes, highest cell temperature °C, charge / discharge limits % |
| `binary_sensor` | `ecoflow_ble` | `found`, `connected`, `authenticated`, `ac_plugged_in` | link state; `ac_plugged_in` (telemetry) — the AC charger is connected |
| `text_sensor` | `ecoflow_ble` | `model`, `serial`, `state`, `error`, `address`, `outputs` | identity and link state; `error` explains a rejection |
| `button` | `ef_provision` | `start_setup`, `exit_setup`, `forget_wifi` | start / leave setup mode (both reboot); forget the stored Wi-Fi |
| `binary_sensor` | `ef_provision` | — | on in setup mode |
| `text_sensor` | `ef_provision` | `status`, `ip` | setup progress; the setup page address |

`ecoflow_ble_id` / `ef_provision_id` can be left out when there is only one.
A command sent while the unit is not authenticated, or for an output the model
does not have, is not sent and logged as a warning.

Home Assistant reaches the device over Wi-Fi, which this configuration keeps
off in normal mode (`wifi: enable_on_boot: false`); with Wi-Fi on in normal
mode BLE and Wi-Fi share the radio — not tested yet.

### Actions

```yaml
- ecoflow_ble.set_output:
    id: unit1
    output: ac          # ac | ac2 | ac_hv | ac_lv | usb | dc
    state: true         # templatable
- ecoflow_ble.set_light:
    id: unit2
    mode: bright        # off | dim | bright | sos (River 3 Plus)
```

Example — the BOOT button: a short press toggles AC, a 3 s press starts setup:

```yaml
binary_sensor:
  - platform: gpio
    name: "BOOT button"
    pin:
      number: GPIO0
      inverted: true
      mode:
        input: true
        pullup: true
    on_click:
      - min_length: 50ms
        max_length: 800ms
        then:
          - switch.toggle: unit1_ac
      - min_length: 3s
        max_length: 10s
        then:
          - button.press: start_setup
```

### Lambda API — `ecoflow_ble`

| Method | |
|---|---|
| `set_output(OUT_AC / OUT_AC2 / OUT_AC_HV / OUT_AC_LV / OUT_USB / OUT_DC, bool)` | switch an output; `false` if not authenticated or the model has no such output |
| `supports(output)`, `outputs_str()` | the model's outputs, e.g. `"ac, dc"` |
| `set_light(LIGHT_OFF / LIGHT_DIM / LIGHT_BRIGHT / LIGHT_SOS)`, `supports_light()` | built-in light |
| `model_name()`, `is_supported()`, `family()` | `"River 3 Plus"`, … or `"Unknown (XXXX)"` |
| `is_found()`, `address()`, `rssi()`, `adv_name()`, `encrypt_type()` | discovery |
| `is_connected()`, `is_authenticated()`, `state_name()`, `last_error()` | link state; `last_error()` explains a rejection (wrong key, not bound, …) |
| `device_sn()`, `slot()`, `set_device_sn(sn)`, `set_user_id(id)` | credentials |
| `send_config(field, bool)`, `send_config_varint(field, value)` | Delta 3 / River 3 / Delta Pro 3: raw `ConfigWrite` field |
| `has_telemetry()`, `telemetry_age_ms()`, `battery_level()`, `input_power()`, `output_power()`, `ac_output_power()`, `dc_output_power()`, … `output_state(OUT_AC)`, `light_mode()`, `telemetry_raw(field)` | telemetry (`optional<>`: empty until reported); field numbers of `DisplayPropertyUpload` |
| `EcoFlowBLE::instance_count()`, `EcoFlowBLE::instance(i)` | all configured units (e.g. for a UI) |
| `set_collect_nearby(bool)`, `nearby()` → `{serial, model, rssi}` | EcoFlow units heard nearby, strongest first |
| `EcoFlowBLE::model_for_serial(sn)` | model name for any serial |

Use `using namespace esphome::ecoflow_ble;` in a lambda for the `OUT_*` /
`LIGHT_*` names.

### Lambda API — `ef_provision`

| Method | |
|---|---|
| `set_active(true)` | run setup mode — only with `auto_start: false`, where you decide the mode yourself (`ef_nvs::want_provisioning()`) |
| `start_setup(forget_wifi = true)` | reboot into setup mode (e.g. from a long button press) |
| `exit_setup()` | leave setup mode and reboot (normal mode if credentials are stored) |
| `forget_wifi()` | erase the Wi-Fi network stored by Improv |
| `is_active()`, `step()`, `ip()`, `wifi_up()`, `message()` | progress for a display (`ProvStep::WAIT_WIFI / AWAIT_LOGIN / AWAIT_DEVICE / DONE`) |

### Adding a unit without the setup page

The setup page only writes NVS; you can write the same keys yourself, e.g. from
a fixed config or your own UI, then reboot:

```yaml
- lambda: |-
    ef_nvs::set_str("user_id", "1234567890123456789");  // from the EcoFlow login
    ef_nvs::set_str("serial",  "R631XXXXXXXXXXXX");     // slot 0
    ef_nvs::set_str("serial1", "R651XXXXXXXXXXXX");     // slot 1 ("serial<N>")
    ef_nvs::set_bool("force_prov", false);
- lambda: 'App.safe_reboot();'
```

Or skip NVS entirely: `ecoflow_ble: { device_sn: "...", user_id: "..." }`.
The `user_id` is printed in the log after a successful login on the setup page
(`login OK ... userId=...`).

### Setup web form (HTTP)

Served by `ef_provision` on port 80 in setup mode, `application/x-www-form-urlencoded`:

| Request | Fields | |
|---|---|---|
| `GET /` | — | the current step (login, unit choice or done) |
| `POST /login` | `email`, `password` | cloud login; on success the unit-choice page |
| `POST /serial` | `sn` (repeat for each ticked unit), `serial` (optional typed serial) | saves units into slots 0, 1, …; clears unused slots |
| `POST /reboot` | — | reboot |

For example: `curl -d "sn=R631XXXXXXXXXXXX&sn=R651XXXXXXXXXXXX" http://<ip>/serial`
(after a login in the same setup session).

## Optional on-device debug UI (LVGL)

[`ui/lvgl_debug.yaml`](ui/lvgl_debug.yaml) is a package for a small landscape
display (240×135) with two buttons; it is not needed for normal operation.

```yaml
packages:
  ecoflow_ui: !include
    file: <path>/esphome-ecoflow-ble/ui/lvgl_debug.yaml
    vars:
      provision_id: ef_prov
      display_id: lcd            # display driving LVGL
      rotation: "90"             # landscape rotation when not flipped
      key_left: btn_left         # binary_sensor: left button
      key_right: btn_right       # binary_sensor: right button
      backlight_id: backlight    # light switching the backlight
      deep_sleep_id: deep_sleep_ctl
```

| Page | |
|---|---|
| Status | unit N/M, model, serial, link state or error, RSSI, encryption, outputs; `Unit` switches units, ⚙ opens Settings |
| Control | the selected unit's AC / USB / DC buttons (only those the model has), result of the last command |
| Light | Off / Dim / Bright / SOS (models with a light) |
| Setup | setup-mode progress, *Start setup* / *Exit setup* |
| Settings | *Flip screen* (180°), *Sleep after* 30 s / 1 min / 3 min / 5 min |

Buttons: left = move the focus (red frame), right = press; with the screen
flipped they swap, so they keep matching what you see. After the chosen time
without a button press the backlight goes off and the ESP32 enters **deep
sleep**; either button wakes it (the wake-up press is not passed on). Nothing
sleeps in setup mode. The device config provides the wakeup pins, see
[`example/ecoflow_lvgl_tdisplay.yaml`](example/ecoflow_lvgl_tdisplay.yaml)
(LilyGO T-Display, two units).

## Examples

| File | |
|---|---|
| [`example/ecoflow.yaml`](example/ecoflow.yaml) | minimal: one unit, setup over Improv, the BOOT button toggles AC |
| [`example/ecoflow_lvgl_tdisplay.yaml`](example/ecoflow_lvgl_tdisplay.yaml) | LilyGO T-Display: two units + debug UI + deep sleep |

Build from the repository root:

```bash
docker run --rm -v "$PWD":/config -w /config ghcr.io/esphome/esphome:2026.10.1 compile example/ecoflow.yaml
```

## Protocol details

- Characteristics are found by the known UUIDs and, failing that, by their properties (WRITE + NOTIFY) — the Delta 3 uses its own UART-style service.
- EcoFlow advertisements: company ID `0xB5B5`, the serial in bytes 1–16, the encryption type in bits 3–5 of byte 22.
- Encryption type 7: ECDH secp160r1 → getKeyInfo → session key `md5(EF_KEYDATA[pos:pos+16] + sRand)`, IV `md5(shared secret)`; 5A5A frames with AES-128-CBC/PKCS7. Type 1: key `md5(serial)`, IV `md5(reversed serial)`, plaintext 5-byte header + zero-padded AES body. Type 0: plain packets.
- Authentication: getAuthStatus, then autoAuth with `md5(user_id + serial)` as uppercase hex; the reply code is decoded into `last_error()`.
- Delta 3 / River 3 / Delta Pro 3: `Packet(0x20, 0x02, 0xFE, 0x11, ConfigWrite, version 0x13)`; fields AC 76, AC2 377, AC HV 15, AC LV 16, USB 19, DC 18, light (`cfg_led_mode`) 89.
- Delta 2 / River 2: packet version 2, `cmd_set 0x20`; AC `0x42` (destination 0x05, Delta 2 Max 0x04), USB `0x22` (0x02), DC `0x51` (0x05).
- Telemetry (Delta 3 / River 3 / Delta Pro 3): the unit sends `src 0x02, cmd_set 0xFE, cmd_id 0x15` about once a second after authentication — protobuf `DisplayPropertyUpload` (payload XORed with `seq[0]`), only the changed fields; the same field numbers in all three families. An output is on when the low two bits of its `flow_info_*` are `10` / `11`.
- The cloud device list cannot be fetched with the login/password token (developer keys are needed); units are found over BLE instead.

## Known quirks

- **esp-idf** only. Crypto uses `esp_rom_md5_*` and hardware `esp_aes_*`.
- `wifi:` must use `enable_on_boot: false`; since ESPHome 2026.10 the Wi-Fi driver is then not initialised in normal mode. `improv_ble` keeps an idle BLE GATT server in normal mode.
- Improv only starts when no Wi-Fi network is stored; `forget_wifi_on_setup` takes care of that. Networks listed under `wifi: networks:` are never forgotten — leave that list empty.
- `ef_provision` enables the CA bundle and dynamic mbedTLS buffers itself (TLS needs ~35 KB while BLE, Wi-Fi and a display run in setup mode). The heap before the login is logged.
- `esp_http_client` and `esp_http_server` are added to the build by the component.
- After adding a new `.cpp` to a component, run `esphome clean` once.
- With `auto_start: false` mind that `ble.enable` is asynchronous on esp-idf: set continuous scanning before it instead of starting a scan right after.
- Telemetry is decoded for the Delta 3 / River 3 / Delta Pro 3 families; Delta 2 / River 2 use another format and are not decoded yet. The unit's time request is not answered (the ESP32 has no clock without Wi-Fi), so values that need it may be missing.

## Licences

- The protocol port is based on [rabits/ha-ef-ble](https://github.com/rabits/ha-ef-ble) — Apache-2.0, see [`LICENSE.ha-ef-ble`](LICENSE.ha-ef-ble).
- `uECC.*` — [micro-ecc](https://github.com/kmackay/micro-ecc), © Kenneth MacKay, BSD 2-clause.
