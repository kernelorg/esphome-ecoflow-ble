[English](README.md) | [Русский](README.ru.md) | **Українська** | [Polski](README.pl.md)

# esphome-ecoflow-ble

Зовнішні компоненти ESPHome для локального керування **портативними
зарядними станціями EcoFlow** через Bluetooth LE з ESP32 (esp-idf). Після
одноразового налаштування пристрій працює без хмари, інтернету та Wi-Fi.

| Компонент | Призначення |
|---|---|
| `ecoflow_ble` | Один блок на станцію: знаходить її за серійним номером, визначає модель і шифрування, авторизується й перемикає виходи (AC / USB / DC) та вбудоване світло, читає заряд, потужність і стан виходів |
| `ef_provision` | Одноразове налаштування: отримує Wi-Fi з телефона через **Improv BLE**, відкриває сторінку налаштування, один раз входить у хмару EcoFlow, дає позначити знайдені поруч станції й зберігає все в NVS |
| `ef_nvs.h` (усередині `ef_provision`) | Хелпери `ef_nvs::get_str/set_str/get_u8/set_u8/get_bool/set_bool/want_provisioning()` для NVS-namespace `ecoflow`, доступні з YAML-лямбд |
| [`ui/lvgl_debug.yaml`](ui/lvgl_debug.yaml) | Необов'язковий налагоджувальний інтерфейс на пристрої (LVGL + дисплей + дві кнопки) |

## Перед початком

- **Станції мають бути вже додані до вашого облікового запису EcoFlow** в
  офіційному застосунку EcoFlow, причому всі — до **одного й того самого**
  облікового запису. ESP32 авторизується з `user_id` цього облікового
  запису; станцію, не прив'язану до нього, станція відхилить («device must be
  set up in the EcoFlow app first» / «device already bound to another
  account»).
- **ESP32 має бути поруч зі станціями.** Bluetooth LE працює на відстані
  кількох метрів, найкраще в одній кімнаті; стіни й металеві корпуси станцій
  зменшують дальність. ESP32 має бути в зоні дії і під час налаштування (щоб
  знайти станції), і в роботі (щоб ними керувати).
- ESP32 підтримує лише Wi-Fi **2,4 ГГц**; Wi-Fi потрібен лише для
  налаштування.

## Підтримувані моделі

Модель визначається за першими символами серійного номера; тип шифрування
(0, 1 або 7) читається з реклами станції.

| Сімейство | Моделі (префікс серійного номера) | Протокол команд | Виходи |
|---|---|---|---|
| Delta 3 | Delta 3 (P231), Delta 3 Plus (P351), Delta 3 Classic (P321), Delta 3 Air (PR11/PR12/PR21), Delta 3 Max (D3N1), Delta 3 Ultra (D751) | protobuf `ConfigWrite` | `ac`, `usb`¹, `dc`¹ |
| Delta 3 (два блоки AC) | Delta 3 Max Plus (D3M1), Delta 3 Ultra Plus (D511) | protobuf `ConfigWrite` | `ac`, `ac2`, `usb`¹, `dc`¹ |
| River 3 | River 3 (R651/R653/R654/R655) | protobuf `ConfigWrite` | `ac`, `dc` |
| River 3 Plus | River 3 Plus (R631/R634/R635) | protobuf `ConfigWrite` | `ac`, `dc`, світло (вимк / тьмяно / яскраво / SOS) |
| Delta Pro 3 | Delta Pro 3 (MR51/MR54) | protobuf `ConfigWrite` | `ac` (= обидва блоки), `ac_hv`, `ac_lv`, `dc`, `usb`¹ |
| Delta 2 | Delta 2 (R331/R335), Delta 2 Max (R351/R354), Delta 3 1500 (D361) | команда на кожен вихід, пакет v2 | `ac`, `usb`, `dc` |
| River 2 | River 2 (R601/R603), River 2 Max (R611/R613), River 2 Pro (R621/R623) | команда на кожен вихід, пакет v2 | `ac`, `dc` |

USB на станціях River 2 та River 3 не перемикається.

Перевірено на залізі:
- **River 3 Plus** ×2 — налаштування через Improv, вхід у хмару, вибір станцій, авторизація, DC, світло, дві станції одночасно;
- **Delta 3 Plus** — авторизація.

Решту реалізовано за протоколом із
[rabits/ha-ef-ble](https://github.com/rabits/ha-ef-ble) і поки не
перевірялося.

¹ Поле є в protobuf `ConfigWrite` цієї моделі, але ha-ef-ble його не
перемикає — вважайте експериментальним.

Інші пристрої EcoFlow (Delta Pro, Delta Max, River Max, PowerStream, Smart
Generator, Wave, …) показуються як `Unknown (XXXX)` і не отримують кнопок
керування.

## Налаштування з телефона (Improv BLE)

Під час першого ввімкнення (облікові дані не збережені) або після *Start
setup* пристрій переходить у **режим налаштування**: через Bluetooth він
оголошує себе як пристрій Improv, а раніше збережена мережа Wi-Fi
забувається. Телефон передає мережу Wi-Fi через **Improv-клієнт**:

- **Android / Windows / macOS / Linux:** https://www.improv-wifi.com у
  Chrome або Edge (Web Bluetooth) → *Connect* → вибрати пристрій.
- **iPhone / iPad:** у Safari та Chrome на iOS немає Web Bluetooth; відкрийте
  https://www.improv-wifi.com у браузері з Web Bluetooth, наприклад
  **Bluefy**.
- **Home Assistant** з Bluetooth поруч із пристроєм знаходить його через
  інтеграцію *Improv via BLE* і пропонує ввести мережу Wi-Fi.

Мережа Wi-Fi має бути **з інтернетом** (потрібен один раз, для входу в
EcoFlow) і **2,4 ГГц**. Два варіанти:

### Варіант 1 — телефон роздає власну точку доступу

Підходить поза домом (роутер не потрібен); вхід іде через мобільний
інтернет телефона.

1. Увімкніть точку доступу на телефоні в діапазоні 2,4 ГГц:
   - Android: *Точка доступу → Діапазон → 2,4 ГГц*;
   - iPhone: *Режим модема → Максимальна сумісність* увімкнено.
2. Не вимикаючи точку доступу, відкрийте Improv-клієнт на **цьому ж
   телефоні**.
3. Підключіться до пристрою й надішліть ім'я та пароль **точки доступу
   телефона**.
4. ESP32 підключається до точки доступу; Improv-клієнт пропонує *Visit
   device*, що відкриває `http://<ip>/` (телефон сам є роутером і бачить
   ESP32 напряму).

### Варіант 2 — наявна мережа Wi-Fi з інтернетом

Підходить удома: і телефон, і ESP32 підключаються до роутера.

1. Підключіть телефон до мережі Wi-Fi (2,4 ГГц або дводіапазонний роутер,
   що роздає й 2,4 ГГц).
2. Відкрийте Improv-клієнт, підключіться до пристрою й надішліть ім'я та
   пароль **цієї мережі**.
3. ESP32 підключається до тієї самої мережі; *Visit device* відкриває
   `http://<ip>/`. Телефон має залишатися в цій мережі (не на мобільному
   інтернеті), щоб відкрити сторінку.

### Сторінка налаштування (обидва варіанти)

1. **Вхід** — e-mail і пароль облікового запису EcoFlow. ESP32 надсилає їх
   на `https://api.ecoflow.com/auth/login` (запасні `api-e` / `api-a`, TLS
   перевіряється за вбудованим набором сертифікатів) і зберігає лише
   `user_id`; пароль не зберігається.
2. **Вибір станцій.** Сторінка показує станції EcoFlow, почуті через
   Bluetooth поруч (модель, серійний номер, сигнал), сильні зверху.
   Позначте не більше, ніж блоків `ecoflow_ble` у прошивці, або введіть
   серійний номер вручну. *Refresh list*, якщо станції немає у списку (чи
   ввімкнена вона й чи поруч?).
3. **Save**, потім *Reboot now* (або *Exit setup* на пристрої). Пристрій
   перезавантажується у звичайний режим: Wi-Fi вимкнено, станції
   знаходяться через BLE й авторизуються.

Щоб згодом змінити станції або обліковий запис, знову увійдіть у режим
налаштування (*Start setup* у налагоджувальному інтерфейсі або кнопка
`ef_provision` з `type: start_setup`, див. [Сутності](#сутності)).

## Конфігурація

```yaml
external_components:
  - source: github://kernelorg/esphome-ecoflow-ble@main
    components:
      - ecoflow_ble
      - ef_provision
  # або локальна копія:
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
  enable_on_boot: false        # його вмикає ef_provision під час завантаження (обидва режими)

wifi:
  enable_on_boot: false        # Wi-Fi лише в режимі налаштування; його вмикає ef_provision

improv_ble:                    # телефон передає мережу Wi-Fi через BLE
  authorizer: none
  next_url: "http://{{ip_address}}/"   # «Visit device» відкриває сторінку налаштування

esp32_ble_tracker:
  scan_parameters:
    active: true
    continuous: false

ef_provision:
  id: ef_prov
  ecoflow_ble_id: unit1        # показувати на сторінці станції, знайдені поруч
  # forget_wifi_on_setup: true # забувати збережений Wi-Fi під час входу в налаштування
  # auto_start: true           # вибрати режим під час завантаження, увімкнути BLE, сканувати

ble_client:                    # по одному на станцію
  - id: ef_client1
    mac_address: 00:00:00:00:00:00
    auto_connect: true
  - id: ef_client2
    mac_address: 00:00:00:00:00:00
    auto_connect: true

ecoflow_ble:                   # по одному на станцію
  - id: unit1
    ble_client_id: ef_client1
    slot: 0                    # 1-ша станція, вибрана на сторінці налаштування
  - id: unit2
    ble_client_id: ef_client2
    slot: 1                    # 2-га вибрана станція
```

### Параметри `ecoflow_ble`

| Параметр | За замовчуванням | |
|---|---|---|
| `ble_client_id` | — | `ble_client` цієї станції (його MAC задається автоматично, коли станцію знайдено) |
| `slot` | — | завантажувати з NVS серійний номер комірки N (0 = перша вибрана станція) і спільний `user_id` |
| `device_sn`, `user_id` | `""` | постійні дані замість `slot` (сторінка налаштування не потрібна) |
| `auto_connect` | `true` | підключатися, щойно видно рекламу станції |

### Параметри `ef_provision`

| Параметр | За замовчуванням | |
|---|---|---|
| `ecoflow_ble_id` | — | будь-який `ecoflow_ble`: тоді сторінка налаштування показує станції поруч |
| `forget_wifi_on_setup` | `true` | стирати збережений Wi-Fi під час входу в режим налаштування, щоб Improv завжди питав мережу |
| `auto_start` | `true` | під час завантаження: режим налаштування, якщо нічого не збережено або налаштування запитане, інакше звичайний режим; вмикає BLE і запускає сканування — `on_boot` не потрібен |

Можна вибрати стільки станцій, скільки блоків `ecoflow_ble` (до 8). ESP32 за
замовчуванням тримає 3 BLE-підключення (`esp32_ble: max_connections`, до 9),
і кожне підключення займає пам'ять — реально 2–3 станції поруч із Wi-Fi та
дисплеєм.

## Використання у власному конфігу

### Сутності

Звичний для ESPHome спосіб — для автоматизацій, вебсторінки або Home Assistant:

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
    type: battery_level          # див. таблицю нижче
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

| Платформа | Компонент | `type` / параметр | |
|---|---|---|---|
| `switch` | `ecoflow_ble` | `output:` `ac`, `ac2`, `ac_hv`, `ac_lv`, `usb`, `dc` | перемикає вихід; стан — справжній, із телеметрії станції (Delta 3 / River 3 / Delta Pro 3). Поки станція не повідомила про вихід — і на Delta 2 / River 2 — це остання надіслана команда (assumed state). Під час завантаження нічого не перемикається |
| `select` | `ecoflow_ble` | — | вбудоване світло: Off / Dim / Bright / SOS |
| `sensor` | `ecoflow_ble` | `rssi` (за замовчуванням), `battery_level`, `input_power`, `output_power`, `ac_input_power`, `ac_output_power`, `dc_output_power`, `usb_c_output_power`, `usb_a_output_power`, `solar_input_power`, `charge_remaining`, `discharge_remaining`, `cell_temperature`, `charge_limit`, `discharge_limit` | RSSI (дБм) працює для всіх моделей; решта — з телеметрії: заряд %, потужність Вт (виходи — додатними числами), залишок часу заряджання / розряджання в хвилинах, максимальна температура комірки °C, пороги заряду / розряду % |
| `binary_sensor` | `ecoflow_ble` | `found`, `connected`, `authenticated`, `ac_plugged_in` | стан зв'язку; `ac_plugged_in` (телеметрія) — підключене заряджання від мережі |
| `text_sensor` | `ecoflow_ble` | `model`, `serial`, `state`, `error`, `address`, `outputs` | дані станції та зв'язку; `error` пояснює відмову |
| `button` | `ef_provision` | `start_setup`, `exit_setup`, `forget_wifi` | увійти в режим налаштування / вийти з нього (обидві з перезавантаженням); забути збережений Wi-Fi |
| `binary_sensor` | `ef_provision` | — | увімкнений у режимі налаштування |
| `text_sensor` | `ef_provision` | `status`, `ip` | хід налаштування; адреса сторінки налаштування |

`ecoflow_ble_id` / `ef_provision_id` можна не вказувати, якщо компонент
один. Команда, надіслана без авторизації або для виходу, якого немає в
моделі, не надсилається й пишеться в лог як попередження.

Home Assistant підключається через Wi-Fi, а ця конфігурація у звичайному
режимі тримає Wi-Fi вимкненим (`wifi: enable_on_boot: false`); з Wi-Fi у
звичайному режимі BLE і Wi-Fi ділять радіо — поки не перевірялося.

### Дії

```yaml
- ecoflow_ble.set_output:
    id: unit1
    output: ac          # ac | ac2 | ac_hv | ac_lv | usb | dc
    state: true         # можна шаблоном
- ecoflow_ble.set_light:
    id: unit2
    mode: bright        # off | dim | bright | sos (River 3 Plus)
```

Приклад — кнопка BOOT: коротке натискання перемикає AC, натискання на 3 с запускає налаштування:

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

### API з лямбд — `ecoflow_ble`

| Метод | |
|---|---|
| `set_output(OUT_AC / OUT_AC2 / OUT_AC_HV / OUT_AC_LV / OUT_USB / OUT_DC, bool)` | перемкнути вихід; `false`, якщо немає авторизації або в моделі немає такого виходу |
| `supports(output)`, `outputs_str()` | виходи моделі, наприклад `"ac, dc"` |
| `set_light(LIGHT_OFF / LIGHT_DIM / LIGHT_BRIGHT / LIGHT_SOS)`, `supports_light()` | вбудоване світло |
| `model_name()`, `is_supported()`, `family()` | `"River 3 Plus"`, … або `"Unknown (XXXX)"` |
| `is_found()`, `address()`, `rssi()`, `adv_name()`, `encrypt_type()` | виявлення |
| `is_connected()`, `is_authenticated()`, `state_name()`, `last_error()` | стан зв'язку; `last_error()` пояснює відмову (неправильний ключ, не прив'язана, …) |
| `device_sn()`, `slot()`, `set_device_sn(sn)`, `set_user_id(id)` | облікові дані |
| `send_config(field, bool)`, `send_config_varint(field, value)` | Delta 3 / River 3 / Delta Pro 3: сире поле `ConfigWrite` |
| `has_telemetry()`, `telemetry_age_ms()`, `battery_level()`, `input_power()`, `output_power()`, `ac_output_power()`, `dc_output_power()`, … `output_state(OUT_AC)`, `light_mode()`, `telemetry_raw(field)` | телеметрія (`optional<>`: порожньо, поки не надійшло); номери полів `DisplayPropertyUpload` |
| `EcoFlowBLE::instance_count()`, `EcoFlowBLE::instance(i)` | усі станції в конфігу (наприклад, для інтерфейсу) |
| `set_collect_nearby(bool)`, `nearby()` → `{serial, model, rssi}` | станції EcoFlow, почуті поруч, сильні першими |
| `EcoFlowBLE::model_for_serial(sn)` | назва моделі для будь-якого серійного номера |

Для імен `OUT_*` / `LIGHT_*` пишіть у лямбді
`using namespace esphome::ecoflow_ble;`.

### API з лямбд — `ef_provision`

| Метод | |
|---|---|
| `set_active(true)` | запустити режим налаштування — лише з `auto_start: false`, коли режим ви обираєте самі (`ef_nvs::want_provisioning()`) |
| `start_setup(forget_wifi = true)` | перезавантажитися в режим налаштування (наприклад, за довгим натисканням кнопки) |
| `exit_setup()` | вийти з режиму налаштування з перезавантаженням (звичайний режим, якщо дані збережено) |
| `forget_wifi()` | стерти мережу Wi-Fi, збережену через Improv |
| `is_active()`, `step()`, `ip()`, `wifi_up()`, `message()` | хід налаштування для дисплея (`ProvStep::WAIT_WIFI / AWAIT_LOGIN / AWAIT_DEVICE / DONE`) |

### Додавання станції без сторінки налаштування

Сторінка налаштування лише записує NVS; ті самі ключі можна записати
самостійно — наприклад, із власного інтерфейсу — і перезавантажитися:

```yaml
- lambda: |-
    ef_nvs::set_str("user_id", "1234567890123456789");  // із входу в EcoFlow
    ef_nvs::set_str("serial",  "R631XXXXXXXXXXXX");     // комірка 0
    ef_nvs::set_str("serial1", "R651XXXXXXXXXXXX");     // комірка 1 ("serial<N>")
    ef_nvs::set_bool("force_prov", false);
- lambda: 'App.safe_reboot();'
```

Або зовсім без NVS: `ecoflow_ble: { device_sn: "...", user_id: "..." }`.
`user_id` виводиться в лог після успішного входу на сторінці налаштування
(`login OK ... userId=...`).

### Вебформа налаштування (HTTP)

Віддається `ef_provision` на порту 80 у режимі налаштування,
`application/x-www-form-urlencoded`:

| Запит | Поля | |
|---|---|---|
| `GET /` | — | поточний крок (вхід, вибір станцій або готово) |
| `POST /login` | `email`, `password` | вхід у хмару; у разі успіху — сторінка вибору станцій |
| `POST /serial` | `sn` (повторити для кожної позначеної станції), `serial` (необов'язковий введений серійний номер) | зберігає станції в комірки 0, 1, …; очищає зайві |
| `POST /reboot` | — | перезавантаження |

Наприклад: `curl -d "sn=R631XXXXXXXXXXXX&sn=R651XXXXXXXXXXXX" http://<ip>/serial`
(після входу в тій самій сесії налаштування).

## Необов'язковий налагоджувальний інтерфейс (LVGL)

[`ui/lvgl_debug.yaml`](ui/lvgl_debug.yaml) — пакет для невеликого
горизонтального дисплея (240×135) з двома кнопками; для звичайної роботи
він не потрібен.

```yaml
packages:
  ecoflow_ui: !include
    file: <path>/esphome-ecoflow-ble/ui/lvgl_debug.yaml
    vars:
      provision_id: ef_prov
      display_id: lcd            # дисплей, на якому працює LVGL
      rotation: "90"             # горизонтальний поворот без перевертання
      key_left: btn_left         # binary_sensor: ліва кнопка
      key_right: btn_right       # binary_sensor: права кнопка
      backlight_id: backlight    # light, що керує підсвічуванням
      deep_sleep_id: deep_sleep_ctl
```

| Сторінка | |
|---|---|
| Status | станція N/M, модель, серійний номер, стан зв'язку або помилка, RSSI, шифрування, виходи; `Unit` перемикає станції, ⚙ відкриває Settings |
| Control | кнопки AC / USB / DC вибраної станції (лише ті, що є в моделі), результат останньої команди |
| Light | Off / Dim / Bright / SOS (моделі зі світлом) |
| Setup | хід налаштування, *Start setup* / *Exit setup* |
| Settings | *Flip screen* (180°), *Sleep after* 30 с / 1 хв / 3 хв / 5 хв |

Кнопки: ліва — перевести фокус (червона рамка), права — натиснути; коли
екран перевернуто, вони міняються місцями й залишаються «лівою» і «правою»
для вас. Через вибраний час без натискань підсвічування гасне, і ESP32
переходить у **deep sleep**; будить будь-яка кнопка (саме натискання
пробудження далі не передається). У режимі налаштування пристрій не
засинає. Виводи пробудження задає конфіг пристрою, див.
[`example/ecoflow_lvgl_tdisplay.yaml`](example/ecoflow_lvgl_tdisplay.yaml)
(LilyGO T-Display, дві станції).

## Приклади

| Файл | |
|---|---|
| [`example/ecoflow.yaml`](example/ecoflow.yaml) | мінімальний: одна станція, налаштування через Improv, кнопка BOOT перемикає AC |
| [`example/ecoflow_lvgl_tdisplay.yaml`](example/ecoflow_lvgl_tdisplay.yaml) | LilyGO T-Display: дві станції + налагоджувальний інтерфейс + deep sleep |

Збирання з кореня репозиторію:

```bash
docker run --rm -v "$PWD":/config -w /config ghcr.io/esphome/esphome:2026.10.1 compile example/ecoflow.yaml
```

## Деталі протоколу

- Характеристики шукаються за відомими UUID, а якщо їх немає — за властивостями (WRITE + NOTIFY): у Delta 3 власний UART-сервіс.
- Реклама EcoFlow: company ID `0xB5B5`, серійний номер у байтах 1–16, тип шифрування в бітах 3–5 байта 22.
- Шифрування типу 7: ECDH secp160r1 → getKeyInfo → ключ сесії `md5(EF_KEYDATA[pos:pos+16] + sRand)`, IV `md5(спільний секрет)`; кадри 5A5A з AES-128-CBC/PKCS7. Тип 1: ключ `md5(серійний номер)`, IV `md5(серійний номер задом наперед)`, відкритий 5-байтний заголовок + тіло AES з доповненням нулями. Тип 0: пакети без шифрування.
- Авторизація: getAuthStatus, потім autoAuth з `md5(user_id + серійний номер)` у вигляді HEX великими літерами; код відповіді розшифровується в `last_error()`.
- Delta 3 / River 3 / Delta Pro 3: `Packet(0x20, 0x02, 0xFE, 0x11, ConfigWrite, version 0x13)`; поля AC 76, AC2 377, AC HV 15, AC LV 16, USB 19, DC 18, світло (`cfg_led_mode`) 89.
- Delta 2 / River 2: версія пакета 2, `cmd_set 0x20`; AC `0x42` (адресат 0x05, у Delta 2 Max 0x04), USB `0x22` (0x02), DC `0x51` (0x05).
- Телеметрія (Delta 3 / River 3 / Delta Pro 3): після авторизації станція приблизно раз на секунду надсилає `src 0x02, cmd_set 0xFE, cmd_id 0x15` — protobuf `DisplayPropertyUpload` (корисне навантаження з XOR за `seq[0]`), лише змінені поля; номери полів однакові в усіх трьох сімействах. Вихід увімкнено, коли два молодші біти його `flow_info_*` дорівнюють `10` / `11`.
- Список пристроїв із хмари за логіном і паролем отримати не можна (потрібні ключі розробника), тому станції шукаються через BLE.

## Відомі особливості

- Лише **esp-idf**. Криптографія — `esp_rom_md5_*` та апаратний `esp_aes_*`.
- `wifi:` потрібен з `enable_on_boot: false`; починаючи з ESPHome 2026.10 Wi-Fi-драйвер тоді не ініціалізується у звичайному режимі. `improv_ble` залишає у звичайному режимі неактивний BLE GATT-сервер.
- Improv запускається, лише якщо мережу Wi-Fi не збережено; про це дбає `forget_wifi_on_setup`. Мережі з `wifi: networks:` не забуваються — залиште цей список порожнім.
- `ef_provision` сам вмикає набір сертифікатів і динамічні буфери mbedTLS (TLS потребує ~35 КБ, поки в режимі налаштування працюють BLE, Wi-Fi та дисплей). Вільна пам'ять перед входом пишеться в лог.
- `esp_http_client` та `esp_http_server` компонент додає до збирання сам.
- Після додавання нового `.cpp` у компонент один раз потрібен `esphome clean`.
- З `auto_start: false` врахуйте, що `ble.enable` на esp-idf асинхронний: задайте безперервне сканування до нього, а не запускайте сканування одразу після.
- Телеметрія розбирається для сімейств Delta 3 / River 3 / Delta Pro 3; Delta 2 / River 2 використовують інший формат і поки не розбираються. На запит часу від станції відповіді немає (без Wi-Fi в ESP32 немає годинника), тому значення, яким він потрібен, можуть бути відсутні.

## Ліцензії

- Порт протоколу базується на [rabits/ha-ef-ble](https://github.com/rabits/ha-ef-ble) — Apache-2.0, див. [`LICENSE.ha-ef-ble`](LICENSE.ha-ef-ble).
- `uECC.*` — [micro-ecc](https://github.com/kmackay/micro-ecc), © Kenneth MacKay, BSD 2-clause.
