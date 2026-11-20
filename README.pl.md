[English](README.md) | [Русский](README.ru.md) | [Українська](README.uk.md) | **Polski**

# esphome-ecoflow-ble

Zewnętrzne komponenty ESPHome do lokalnego sterowania **przenośnymi stacjami
zasilania EcoFlow** przez Bluetooth LE z ESP32 (esp-idf). Po jednorazowej
konfiguracji urządzenie działa bez chmury, internetu i Wi-Fi.

| Komponent | Przeznaczenie |
|---|---|
| `ecoflow_ble` | Jeden blok na stację: znajduje ją po numerze seryjnym, rozpoznaje model i szyfrowanie, uwierzytelnia się i przełącza wyjścia (AC / USB / DC) oraz wbudowane światło, odczytuje baterię, moc i stan wyjść |
| `ef_provision` | Jednorazowa konfiguracja: pobiera Wi-Fi z telefonu przez **Improv BLE**, udostępnia stronę konfiguracji, raz loguje się do chmury EcoFlow, pozwala zaznaczyć stacje znalezione w pobliżu i zapisuje wszystko w NVS |
| `ef_nvs.h` (w `ef_provision`) | Funkcje `ef_nvs::get_str/set_str/get_u8/set_u8/get_bool/set_bool/want_provisioning()` dla przestrzeni NVS `ecoflow`, dostępne z lambd YAML |
| [`ui/lvgl_debug.yaml`](ui/lvgl_debug.yaml) | Opcjonalny interfejs diagnostyczny na urządzeniu (LVGL + wyświetlacz + dwa przyciski) |

## Zanim zaczniesz

- **Stacje muszą być już dodane do Twojego konta EcoFlow** w oficjalnej
  aplikacji EcoFlow, i to wszystkie do **tego samego** konta. ESP32
  uwierzytelnia się `user_id` tego konta; stacja niepowiązana z nim odrzuci
  połączenie („device must be set up in the EcoFlow app first” / „device
  already bound to another account”).
- **ESP32 musi być blisko stacji.** Bluetooth LE działa na odległość kilku
  metrów, najlepiej w jednym pomieszczeniu; ściany i metalowe obudowy stacji
  zmniejszają zasięg. ESP32 musi być w zasięgu zarówno podczas konfiguracji
  (aby znaleźć stacje), jak i w trakcie pracy (aby nimi sterować).
- ESP32 obsługuje tylko Wi-Fi **2,4 GHz**; Wi-Fi jest potrzebne tylko do
  konfiguracji.

## Obsługiwane modele

Model jest rozpoznawany po pierwszych znakach numeru seryjnego; typ
szyfrowania (0, 1 lub 7) jest odczytywany z reklamy stacji.

| Rodzina | Modele (prefiks numeru seryjnego) | Protokół poleceń | Wyjścia |
|---|---|---|---|
| Delta 3 | Delta 3 (P231), Delta 3 Plus (P351), Delta 3 Classic (P321), Delta 3 Air (PR11/PR12/PR21), Delta 3 Max (D3N1), Delta 3 Ultra (D751) | protobuf `ConfigWrite` | `ac`, `usb`¹, `dc`¹ |
| Delta 3 (dwa bloki AC) | Delta 3 Max Plus (D3M1), Delta 3 Ultra Plus (D511) | protobuf `ConfigWrite` | `ac`, `ac2`, `usb`¹, `dc`¹ |
| River 3 | River 3 (R651/R653/R654/R655) | protobuf `ConfigWrite` | `ac`, `dc` |
| River 3 Plus | River 3 Plus (R631/R634/R635) | protobuf `ConfigWrite` | `ac`, `dc`, światło (wył. / przyciemnione / jasne / SOS) |
| Delta Pro 3 | Delta Pro 3 (MR51/MR54) | protobuf `ConfigWrite` | `ac` (= oba bloki), `ac_hv`, `ac_lv`, `dc`, `usb`¹ |
| Delta 2 | Delta 2 (R331/R335), Delta 2 Max (R351/R354), Delta 3 1500 (D361) | osobne polecenie na wyjście, pakiet v2 | `ac`, `usb`, `dc` |
| River 2 | River 2 (R601/R603), River 2 Max (R611/R613), River 2 Pro (R621/R623) | osobne polecenie na wyjście, pakiet v2 | `ac`, `dc` |

USB w stacjach River 2 i River 3 nie da się przełączać.

Sprawdzone na sprzęcie:
- **River 3 Plus** ×2 — konfiguracja przez Improv, logowanie do chmury, wybór stacji, uwierzytelnianie, DC, światło, dwie stacje jednocześnie;
- **Delta 3 Plus** — uwierzytelnianie.

Pozostałe zaimplementowano według protokołu z
[rabits/ha-ef-ble](https://github.com/rabits/ha-ef-ble) i nie były jeszcze
testowane.

¹ Pole istnieje w protobuf `ConfigWrite` tego modelu, ale ha-ef-ble go nie
przełącza — traktuj je jako eksperymentalne.

Inne urządzenia EcoFlow (Delta Pro, Delta Max, River Max, PowerStream, Smart
Generator, Wave, …) są wyświetlane jako `Unknown (XXXX)` i nie mają
przycisków sterowania.

## Konfiguracja z telefonu (Improv BLE)

Przy pierwszym uruchomieniu (brak zapisanych danych) lub po *Start setup*
urządzenie przechodzi w **tryb konfiguracji**: przez Bluetooth ogłasza się
jako urządzenie Improv, a wcześniej zapisana sieć Wi-Fi jest zapominana.
Telefon przesyła sieć Wi-Fi przez **klienta Improv**:

- **Android / Windows / macOS / Linux:** https://www.improv-wifi.com w
  Chrome lub Edge (Web Bluetooth) → *Connect* → wybierz urządzenie.
- **iPhone / iPad:** Safari i Chrome na iOS nie mają Web Bluetooth; otwórz
  https://www.improv-wifi.com w przeglądarce z Web Bluetooth, np. **Bluefy**.
- **Home Assistant** z Bluetooth w pobliżu urządzenia wykrywa je przez
  integrację *Improv via BLE* i prosi o sieć Wi-Fi.

Sieć Wi-Fi musi mieć **dostęp do internetu** (potrzebny raz, do logowania w
EcoFlow) i działać w paśmie **2,4 GHz**. Dwa warianty:

### Wariant 1 — telefon udostępnia własny hotspot

Do użytku poza domem (router niepotrzebny); logowanie idzie przez internet
mobilny telefonu.

1. Włącz hotspot w telefonie w paśmie 2,4 GHz:
   - Android: *Hotspot → Pasmo → 2,4 GHz*;
   - iPhone: *Hotspot osobisty → Maksymalizuj zgodność* włączone.
2. Nie wyłączając hotspotu, otwórz klienta Improv na **tym samym
   telefonie**.
3. Połącz się z urządzeniem i wyślij nazwę i hasło **hotspotu telefonu**.
4. ESP32 łączy się z hotspotem; klient Improv proponuje *Visit device*,
   które otwiera `http://<ip>/` (telefon sam jest routerem i widzi ESP32
   bezpośrednio).

### Wariant 2 — istniejąca sieć Wi-Fi z internetem

Do użytku w domu: telefon i ESP32 łączą się z routerem.

1. Połącz telefon z siecią Wi-Fi (2,4 GHz lub router dwupasmowy, który
   nadaje też 2,4 GHz).
2. Otwórz klienta Improv, połącz się z urządzeniem i wyślij nazwę i hasło
   **tej sieci**.
3. ESP32 łączy się z tą samą siecią; *Visit device* otwiera
   `http://<ip>/`. Aby otworzyć stronę, telefon musi pozostać w tej sieci
   (nie na internecie mobilnym).

### Strona konfiguracji (oba warianty)

1. **Logowanie** — e-mail i hasło konta EcoFlow. ESP32 wysyła je na
   `https://api.ecoflow.com/auth/login` (zapasowe `api-e` / `api-a`, TLS
   sprawdzany wbudowanym zestawem certyfikatów) i zapisuje tylko `user_id`;
   hasło nie jest zapisywane.
2. **Wybór stacji.** Strona pokazuje stacje EcoFlow słyszane przez Bluetooth
   w pobliżu (model, numer seryjny, sygnał), najsilniejsze na górze. Zaznacz
   nie więcej, niż jest bloków `ecoflow_ble` w firmware, albo wpisz numer
   seryjny ręcznie. *Refresh list*, jeśli stacji brakuje (czy jest włączona
   i blisko?).
3. **Save**, potem *Reboot now* (lub *Exit setup* na urządzeniu). Urządzenie
   uruchamia się ponownie w zwykłym trybie: Wi-Fi wyłączone, stacje są
   znajdowane przez BLE i uwierzytelniane.

Aby później zmienić stacje lub konto, wejdź ponownie w tryb konfiguracji
(*Start setup* w interfejsie diagnostycznym lub przycisk `ef_provision` z
`type: start_setup`, zob. [Encje](#encje)).

## Konfiguracja YAML

```yaml
external_components:
  - source: github://kernelorg/esphome-ecoflow-ble@main
    components:
      - ecoflow_ble
      - ef_provision
  # albo lokalna kopia:
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
  enable_on_boot: false        # włącza je ef_provision przy starcie (oba tryby)

wifi:
  enable_on_boot: false        # Wi-Fi tylko w trybie konfiguracji; włącza je ef_provision

improv_ble:                    # telefon przesyła sieć Wi-Fi przez BLE
  authorizer: none
  next_url: "http://{{ip_address}}/"   # „Visit device” otwiera stronę konfiguracji

esp32_ble_tracker:
  scan_parameters:
    active: true
    continuous: false

ef_provision:
  id: ef_prov
  ecoflow_ble_id: unit1        # pokazuj na stronie stacje znalezione w pobliżu
  # forget_wifi_on_setup: true # zapominaj zapisane Wi-Fi przy wejściu w konfigurację
  # auto_start: true           # wybierz tryb przy starcie, włącz BLE, skanuj

ble_client:                    # jeden na stację
  - id: ef_client1
    mac_address: 00:00:00:00:00:00
    auto_connect: true
  - id: ef_client2
    mac_address: 00:00:00:00:00:00
    auto_connect: true

ecoflow_ble:                   # jeden na stację
  - id: unit1
    ble_client_id: ef_client1
    slot: 0                    # 1. stacja wybrana na stronie konfiguracji
  - id: unit2
    ble_client_id: ef_client2
    slot: 1                    # 2. wybrana stacja
```

### Opcje `ecoflow_ble`

| Opcja | Domyślnie | |
|---|---|---|
| `ble_client_id` | — | `ble_client` tej stacji (jego MAC jest ustawiany automatycznie po znalezieniu stacji) |
| `slot` | — | wczytaj z NVS numer seryjny slotu N (0 = pierwsza wybrana stacja) i wspólny `user_id` |
| `device_sn`, `user_id` | `""` | stałe dane zamiast `slot` (strona konfiguracji niepotrzebna) |
| `auto_connect` | `true` | łącz się, gdy tylko widać reklamę stacji |

### Opcje `ef_provision`

| Opcja | Domyślnie | |
|---|---|---|
| `ecoflow_ble_id` | — | dowolny `ecoflow_ble`: strona konfiguracji pokazuje wtedy stacje w pobliżu |
| `forget_wifi_on_setup` | `true` | kasuj zapisane Wi-Fi przy wejściu w tryb konfiguracji, aby Improv zawsze pytał o sieć |
| `auto_start` | `true` | przy starcie: tryb konfiguracji, gdy nic nie zapisano lub zażądano konfiguracji, inaczej zwykły tryb; włącza BLE i uruchamia skanowanie — `on_boot` niepotrzebny |

Można wybrać tyle stacji, ile jest bloków `ecoflow_ble` (do 8). ESP32
domyślnie utrzymuje 3 połączenia BLE (`esp32_ble: max_connections`, do 9), a
każde połączenie zużywa pamięć — realnie 2–3 stacje obok Wi-Fi i
wyświetlacza.

## Użycie we własnej konfiguracji

### Encje

Zwykły sposób ESPHome — dla automatyzacji, strony WWW lub Home Assistant:

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
    type: battery_level          # zob. tabelę poniżej
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

| Platforma | Komponent | `type` / opcja | |
|---|---|---|---|
| `switch` | `ecoflow_ble` | `output:` `ac`, `ac2`, `ac_hv`, `ac_lv`, `usb`, `dc` | przełącza wyjście; stan jest prawdziwy, z telemetrii stacji (Delta 3 / River 3 / Delta Pro 3). Dopóki stacja nie zgłosi wyjścia — i w Delta 2 / River 2 — jest to ostatnie wysłane polecenie (assumed state). Przy starcie nic nie jest przełączane |
| `select` | `ecoflow_ble` | — | wbudowane światło: Off / Dim / Bright / SOS |
| `sensor` | `ecoflow_ble` | `rssi` (domyślnie), `battery_level`, `input_power`, `output_power`, `ac_input_power`, `ac_output_power`, `dc_output_power`, `usb_c_output_power`, `usb_a_output_power`, `solar_input_power`, `charge_remaining`, `discharge_remaining`, `cell_temperature`, `charge_limit`, `discharge_limit` | RSSI (dBm) działa dla każdego modelu; reszta pochodzi z telemetrii: bateria %, moc W (wyjścia jako wartości dodatnie), pozostały czas ładowania / rozładowania w minutach, najwyższa temperatura ogniwa °C, limity ładowania / rozładowania % |
| `binary_sensor` | `ecoflow_ble` | `found`, `connected`, `authenticated`, `ac_plugged_in` | stan połączenia; `ac_plugged_in` (telemetria) — podłączone ładowanie z sieci |
| `text_sensor` | `ecoflow_ble` | `model`, `serial`, `state`, `error`, `address`, `outputs` | dane stacji i połączenia; `error` wyjaśnia odmowę |
| `button` | `ef_provision` | `start_setup`, `exit_setup`, `forget_wifi` | wejście / wyjście z trybu konfiguracji (oba z restartem); zapomnij zapisane Wi-Fi |
| `binary_sensor` | `ef_provision` | — | włączony w trybie konfiguracji |
| `text_sensor` | `ef_provision` | `status`, `ip` | postęp konfiguracji; adres strony konfiguracji |

`ecoflow_ble_id` / `ef_provision_id` można pominąć, gdy komponent jest
jeden. Polecenie wysłane bez uwierzytelnienia lub dla wyjścia, którego model
nie ma, nie jest wysyłane i trafia do logu jako ostrzeżenie.

Home Assistant łączy się przez Wi-Fi, a ta konfiguracja w zwykłym trybie
trzyma Wi-Fi wyłączone (`wifi: enable_on_boot: false`); z Wi-Fi w zwykłym
trybie BLE i Wi-Fi dzielą radio — jeszcze nie testowane.

### Akcje

```yaml
- ecoflow_ble.set_output:
    id: unit1
    output: ac          # ac | ac2 | ac_hv | ac_lv | usb | dc
    state: true         # może być szablonem
- ecoflow_ble.set_light:
    id: unit2
    mode: bright        # off | dim | bright | sos (River 3 Plus)
```

Przykład — przycisk BOOT: krótkie naciśnięcie przełącza AC, naciśnięcie przez 3 s uruchamia konfigurację:

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

### API z lambd — `ecoflow_ble`

| Metoda | |
|---|---|
| `set_output(OUT_AC / OUT_AC2 / OUT_AC_HV / OUT_AC_LV / OUT_USB / OUT_DC, bool)` | przełączenie wyjścia; `false`, jeśli brak uwierzytelnienia lub model nie ma takiego wyjścia |
| `supports(output)`, `outputs_str()` | wyjścia modelu, np. `"ac, dc"` |
| `set_light(LIGHT_OFF / LIGHT_DIM / LIGHT_BRIGHT / LIGHT_SOS)`, `supports_light()` | wbudowane światło |
| `model_name()`, `is_supported()`, `family()` | `"River 3 Plus"`, … lub `"Unknown (XXXX)"` |
| `is_found()`, `address()`, `rssi()`, `adv_name()`, `encrypt_type()` | wykrywanie |
| `is_connected()`, `is_authenticated()`, `state_name()`, `last_error()` | stan połączenia; `last_error()` wyjaśnia odmowę (zły klucz, niepowiązana, …) |
| `device_sn()`, `slot()`, `set_device_sn(sn)`, `set_user_id(id)` | dane uwierzytelniające |
| `send_config(field, bool)`, `send_config_varint(field, value)` | Delta 3 / River 3 / Delta Pro 3: surowe pole `ConfigWrite` |
| `has_telemetry()`, `telemetry_age_ms()`, `battery_level()`, `input_power()`, `output_power()`, `ac_output_power()`, `dc_output_power()`, … `output_state(OUT_AC)`, `light_mode()`, `telemetry_raw(field)` | telemetria (`optional<>`: puste, dopóki nie nadejdzie); numery pól `DisplayPropertyUpload` |
| `EcoFlowBLE::instance_count()`, `EcoFlowBLE::instance(i)` | wszystkie stacje w konfiguracji (np. dla interfejsu) |
| `set_collect_nearby(bool)`, `nearby()` → `{serial, model, rssi}` | stacje EcoFlow słyszane w pobliżu, najsilniejsze pierwsze |
| `EcoFlowBLE::model_for_serial(sn)` | nazwa modelu dla dowolnego numeru seryjnego |

Dla nazw `OUT_*` / `LIGHT_*` napisz w lambdzie
`using namespace esphome::ecoflow_ble;`.

### API z lambd — `ef_provision`

| Metoda | |
|---|---|
| `set_active(true)` | uruchom tryb konfiguracji — tylko z `auto_start: false`, gdy tryb wybierasz sam (`ef_nvs::want_provisioning()`) |
| `start_setup(forget_wifi = true)` | uruchom ponownie w trybie konfiguracji (np. długim naciśnięciem przycisku) |
| `exit_setup()` | wyjdź z trybu konfiguracji z ponownym uruchomieniem (zwykły tryb, jeśli dane są zapisane) |
| `forget_wifi()` | skasuj sieć Wi-Fi zapisaną przez Improv |
| `is_active()`, `step()`, `ip()`, `wifi_up()`, `message()` | postęp konfiguracji dla wyświetlacza (`ProvStep::WAIT_WIFI / AWAIT_LOGIN / AWAIT_DEVICE / DONE`) |

### Dodanie stacji bez strony konfiguracji

Strona konfiguracji tylko zapisuje NVS; te same klucze można zapisać samemu —
np. z własnego interfejsu — i uruchomić ponownie:

```yaml
- lambda: |-
    ef_nvs::set_str("user_id", "1234567890123456789");  // z logowania do EcoFlow
    ef_nvs::set_str("serial",  "R631XXXXXXXXXXXX");     // slot 0
    ef_nvs::set_str("serial1", "R651XXXXXXXXXXXX");     // slot 1 ("serial<N>")
    ef_nvs::set_bool("force_prov", false);
- lambda: 'App.safe_reboot();'
```

Albo całkiem bez NVS: `ecoflow_ble: { device_sn: "...", user_id: "..." }`.
`user_id` jest wypisywany w logu po udanym logowaniu na stronie konfiguracji
(`login OK ... userId=...`).

### Formularz konfiguracji (HTTP)

Udostępniany przez `ef_provision` na porcie 80 w trybie konfiguracji,
`application/x-www-form-urlencoded`:

| Żądanie | Pola | |
|---|---|---|
| `GET /` | — | bieżący krok (logowanie, wybór stacji lub gotowe) |
| `POST /login` | `email`, `password` | logowanie do chmury; po sukcesie — strona wyboru stacji |
| `POST /serial` | `sn` (powtórz dla każdej zaznaczonej stacji), `serial` (opcjonalny wpisany numer seryjny) | zapisuje stacje w slotach 0, 1, …; czyści nieużywane |
| `POST /reboot` | — | ponowne uruchomienie |

Na przykład: `curl -d "sn=R631XXXXXXXXXXXX&sn=R651XXXXXXXXXXXX" http://<ip>/serial`
(po zalogowaniu w tej samej sesji konfiguracji).

## Opcjonalny interfejs diagnostyczny (LVGL)

[`ui/lvgl_debug.yaml`](ui/lvgl_debug.yaml) to pakiet dla małego poziomego
wyświetlacza (240×135) z dwoma przyciskami; do zwykłej pracy nie jest
potrzebny.

```yaml
packages:
  ecoflow_ui: !include
    file: <path>/esphome-ecoflow-ble/ui/lvgl_debug.yaml
    vars:
      provision_id: ef_prov
      display_id: lcd            # wyświetlacz, na którym działa LVGL
      rotation: "90"             # obrót poziomy bez odwrócenia
      key_left: btn_left         # binary_sensor: lewy przycisk
      key_right: btn_right       # binary_sensor: prawy przycisk
      backlight_id: backlight    # light sterujący podświetleniem
      deep_sleep_id: deep_sleep_ctl
```

| Strona | |
|---|---|
| Status | stacja N/M, model, numer seryjny, stan połączenia lub błąd, RSSI, szyfrowanie, wyjścia; `Unit` przełącza stacje, ⚙ otwiera Settings |
| Control | przyciski AC / USB / DC wybranej stacji (tylko te, które ma model), wynik ostatniego polecenia |
| Light | Off / Dim / Bright / SOS (modele ze światłem) |
| Setup | postęp konfiguracji, *Start setup* / *Exit setup* |
| Settings | *Flip screen* (180°), *Sleep after* 30 s / 1 min / 3 min / 5 min |

Przyciski: lewy — przesuń fokus (czerwona ramka), prawy — naciśnij; po
odwróceniu ekranu zamieniają się rolami, więc pozostają „lewym” i „prawym”
dla Ciebie. Po wybranym czasie bez naciśnięć podświetlenie gaśnie, a ESP32
przechodzi w **deep sleep**; budzi dowolny przycisk (samo naciśnięcie
budzące nie jest dalej przekazywane). W trybie konfiguracji urządzenie nie
zasypia. Piny wybudzania definiuje konfiguracja urządzenia, zob.
[`example/ecoflow_lvgl_tdisplay.yaml`](example/ecoflow_lvgl_tdisplay.yaml)
(LilyGO T-Display, dwie stacje).

## Przykłady

| Plik | |
|---|---|
| [`example/ecoflow.yaml`](example/ecoflow.yaml) | minimalny: jedna stacja, konfiguracja przez Improv, przycisk BOOT przełącza AC |
| [`example/ecoflow_lvgl_tdisplay.yaml`](example/ecoflow_lvgl_tdisplay.yaml) | LilyGO T-Display: dwie stacje + interfejs diagnostyczny + deep sleep |

Budowanie z katalogu głównego repozytorium:

```bash
docker run --rm -v "$PWD":/config -w /config ghcr.io/esphome/esphome:2026.10.1 compile example/ecoflow.yaml
```

## Szczegóły protokołu

- Charakterystyki są wyszukiwane po znanych UUID, a w razie ich braku po właściwościach (WRITE + NOTIFY): Delta 3 ma własną usługę typu UART.
- Reklamy EcoFlow: company ID `0xB5B5`, numer seryjny w bajtach 1–16, typ szyfrowania w bitach 3–5 bajtu 22.
- Szyfrowanie typu 7: ECDH secp160r1 → getKeyInfo → klucz sesji `md5(EF_KEYDATA[pos:pos+16] + sRand)`, IV `md5(wspólny sekret)`; ramki 5A5A z AES-128-CBC/PKCS7. Typ 1: klucz `md5(numer seryjny)`, IV `md5(odwrócony numer seryjny)`, jawny 5-bajtowy nagłówek + treść AES dopełniona zerami. Typ 0: pakiety bez szyfrowania.
- Uwierzytelnianie: getAuthStatus, potem autoAuth z `md5(user_id + numer seryjny)` jako HEX wielkimi literami; kod odpowiedzi jest dekodowany do `last_error()`.
- Delta 3 / River 3 / Delta Pro 3: `Packet(0x20, 0x02, 0xFE, 0x11, ConfigWrite, version 0x13)`; pola AC 76, AC2 377, AC HV 15, AC LV 16, USB 19, DC 18, światło (`cfg_led_mode`) 89.
- Delta 2 / River 2: wersja pakietu 2, `cmd_set 0x20`; AC `0x42` (adresat 0x05, w Delta 2 Max 0x04), USB `0x22` (0x02), DC `0x51` (0x05).
- Telemetria (Delta 3 / River 3 / Delta Pro 3): po uwierzytelnieniu stacja mniej więcej co sekundę wysyła `src 0x02, cmd_set 0xFE, cmd_id 0x15` — protobuf `DisplayPropertyUpload` (dane z XOR przez `seq[0]`), tylko zmienione pola; numery pól są takie same we wszystkich trzech rodzinach. Wyjście jest włączone, gdy dwa najmłodsze bity jego `flow_info_*` to `10` / `11`.
- Listy urządzeń z chmury nie da się pobrać tokenem z loginu i hasła (potrzebne są klucze deweloperskie), dlatego stacje są wyszukiwane przez BLE.

## Znane osobliwości

- Tylko **esp-idf**. Kryptografia: `esp_rom_md5_*` i sprzętowe `esp_aes_*`.
- `wifi:` musi mieć `enable_on_boot: false`; od ESPHome 2026.10 sterownik Wi-Fi nie jest wtedy inicjalizowany w zwykłym trybie. `improv_ble` pozostawia w zwykłym trybie nieaktywny serwer BLE GATT.
- Improv uruchamia się tylko wtedy, gdy nie zapisano sieci Wi-Fi; dba o to `forget_wifi_on_setup`. Sieci z `wifi: networks:` nie są zapominane — zostaw tę listę pustą.
- `ef_provision` sam włącza zestaw certyfikatów i dynamiczne bufory mbedTLS (TLS potrzebuje ~35 KB, gdy w trybie konfiguracji działają BLE, Wi-Fi i wyświetlacz). Wolna pamięć przed logowaniem jest zapisywana w logu.
- `esp_http_client` i `esp_http_server` komponent dodaje do buildu sam.
- Po dodaniu nowego pliku `.cpp` do komponentu trzeba raz wykonać `esphome clean`.
- Z `auto_start: false` pamiętaj, że `ble.enable` na esp-idf działa asynchronicznie: ustaw ciągłe skanowanie przed nim zamiast uruchamiać skanowanie zaraz po.
- Telemetria jest dekodowana dla rodzin Delta 3 / River 3 / Delta Pro 3; Delta 2 / River 2 używają innego formatu i nie są jeszcze dekodowane. Zapytanie stacji o czas pozostaje bez odpowiedzi (bez Wi-Fi ESP32 nie ma zegara), więc wartości, które go wymagają, mogą być niedostępne.

## Licencje

- Port protokołu bazuje na [rabits/ha-ef-ble](https://github.com/rabits/ha-ef-ble) — Apache-2.0, zob. [`LICENSE.ha-ef-ble`](LICENSE.ha-ef-ble).
- `uECC.*` — [micro-ecc](https://github.com/kmackay/micro-ecc), © Kenneth MacKay, BSD 2-clause.
