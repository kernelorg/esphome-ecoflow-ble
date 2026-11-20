[English](README.md) | **Русский** | [Українська](README.uk.md) | [Polski](README.pl.md)

# esphome-ecoflow-ble

Внешние компоненты ESPHome для локального управления **портативными
зарядными станциями EcoFlow** по Bluetooth LE с ESP32 (esp-idf). После
разовой настройки устройство работает без облака, интернета и Wi-Fi.

| Компонент | Назначение |
|---|---|
| `ecoflow_ble` | Один блок на станцию: находит её по серийному номеру, определяет модель и шифрование, авторизуется и переключает выходы (AC / USB / DC) и встроенный свет, читает заряд, мощность и состояние выходов |
| `ef_provision` | Разовая настройка: получает Wi-Fi с телефона через **Improv BLE**, открывает страницу настройки, один раз входит в облако EcoFlow, даёт отметить найденные рядом станции и сохраняет всё в NVS |
| `ef_nvs.h` (внутри `ef_provision`) | Хелперы `ef_nvs::get_str/set_str/get_u8/set_u8/get_bool/set_bool/want_provisioning()` для NVS-namespace `ecoflow`, доступны из YAML-лямбд |
| [`ui/lvgl_debug.yaml`](ui/lvgl_debug.yaml) | Необязательный отладочный интерфейс на устройстве (LVGL + дисплей + две кнопки) |

## Перед началом

- **Станции должны быть уже добавлены в ваш аккаунт EcoFlow** в официальном
  приложении EcoFlow, причём все — в **один и тот же** аккаунт. ESP32
  авторизуется с `user_id` этого аккаунта; станцию, не привязанную к нему,
  станция отклонит («device must be set up in the EcoFlow app first» /
  «device already bound to another account»).
- **ESP32 должен быть рядом со станциями.** Bluetooth LE работает на
  расстоянии нескольких метров, лучше всего в одной комнате; стены и
  металлические корпуса станций уменьшают дальность. ESP32 должен быть в
  зоне действия и при настройке (чтобы найти станции), и в работе (чтобы ими
  управлять).
- ESP32 поддерживает только Wi-Fi **2,4 ГГц**; Wi-Fi нужен только для
  настройки.

## Поддерживаемые модели

Модель определяется по первым символам серийного номера; тип шифрования
(0, 1 или 7) читается из рекламы станции.

| Семейство | Модели (префикс серийника) | Протокол команд | Выходы |
|---|---|---|---|
| Delta 3 | Delta 3 (P231), Delta 3 Plus (P351), Delta 3 Classic (P321), Delta 3 Air (PR11/PR12/PR21), Delta 3 Max (D3N1), Delta 3 Ultra (D751) | protobuf `ConfigWrite` | `ac`, `usb`¹, `dc`¹ |
| Delta 3 (два блока AC) | Delta 3 Max Plus (D3M1), Delta 3 Ultra Plus (D511) | protobuf `ConfigWrite` | `ac`, `ac2`, `usb`¹, `dc`¹ |
| River 3 | River 3 (R651/R653/R654/R655) | protobuf `ConfigWrite` | `ac`, `dc` |
| River 3 Plus | River 3 Plus (R631/R634/R635) | protobuf `ConfigWrite` | `ac`, `dc`, свет (выкл / тускло / ярко / SOS) |
| Delta Pro 3 | Delta Pro 3 (MR51/MR54) | protobuf `ConfigWrite` | `ac` (= оба блока), `ac_hv`, `ac_lv`, `dc`, `usb`¹ |
| Delta 2 | Delta 2 (R331/R335), Delta 2 Max (R351/R354), Delta 3 1500 (D361) | команда на каждый выход, пакет v2 | `ac`, `usb`, `dc` |
| River 2 | River 2 (R601/R603), River 2 Max (R611/R613), River 2 Pro (R621/R623) | команда на каждый выход, пакет v2 | `ac`, `dc` |

USB на станциях River 2 и River 3 не переключается.

Проверено на железе:
- **River 3 Plus** ×2 — настройка через Improv, вход в облако, выбор станций, авторизация, DC, свет, две станции одновременно;
- **Delta 3 Plus** — авторизация.

Остальное реализовано по протоколу из
[rabits/ha-ef-ble](https://github.com/rabits/ha-ef-ble) и пока не
проверялось.

¹ Поле есть в protobuf `ConfigWrite` этой модели, но ha-ef-ble его не
переключает — считайте экспериментальным.

Другие устройства EcoFlow (Delta Pro, Delta Max, River Max, PowerStream,
Smart Generator, Wave, …) показываются как `Unknown (XXXX)` и не получают
кнопок управления.

## Настройка с телефона (Improv BLE)

При первом включении (учётные данные не сохранены) или после *Start setup*
устройство входит в **режим настройки**: по Bluetooth оно объявляет себя как
устройство Improv, а ранее сохранённая сеть Wi-Fi забывается. Телефон
передаёт сеть Wi-Fi через **Improv-клиент**:

- **Android / Windows / macOS / Linux:** https://www.improv-wifi.com в
  Chrome или Edge (Web Bluetooth) → *Connect* → выбрать устройство.
- **iPhone / iPad:** в Safari и Chrome на iOS нет Web Bluetooth; откройте
  https://www.improv-wifi.com в браузере с Web Bluetooth, например **Bluefy**.
- **Home Assistant** с Bluetooth рядом с устройством находит его через
  интеграцию *Improv via BLE* и предлагает ввести сеть Wi-Fi.

Сеть Wi-Fi должна быть **с интернетом** (нужен один раз, для входа в
EcoFlow) и **2,4 ГГц**. Два варианта:

### Вариант 1 — телефон раздаёт свою точку доступа

Подходит вне дома (роутер не нужен); вход идёт через мобильный интернет
телефона.

1. Включите точку доступа на телефоне в диапазоне 2,4 ГГц:
   - Android: *Точка доступа → Диапазон → 2,4 ГГц*;
   - iPhone: *Режим модема → Максимальная совместимость* включено.
2. Не выключая точку доступа, откройте Improv-клиент на **этом же
   телефоне**.
3. Подключитесь к устройству и отправьте имя и пароль **точки доступа
   телефона**.
4. ESP32 подключается к точке доступа; Improv-клиент предлагает *Visit
   device*, который открывает `http://<ip>/` (телефон сам является роутером
   и видит ESP32 напрямую).

### Вариант 2 — существующая сеть Wi-Fi с интернетом

Подходит дома: и телефон, и ESP32 подключаются к роутеру.

1. Подключите телефон к сети Wi-Fi (2,4 ГГц или двухдиапазонный роутер,
   который раздаёт и 2,4 ГГц).
2. Откройте Improv-клиент, подключитесь к устройству и отправьте имя и
   пароль **этой сети**.
3. ESP32 подключается к той же сети; *Visit device* открывает
   `http://<ip>/`. Телефон должен оставаться в этой сети (не на мобильном
   интернете), чтобы открыть страницу.

### Страница настройки (оба варианта)

1. **Вход** — e-mail и пароль аккаунта EcoFlow. ESP32 отправляет их на
   `https://api.ecoflow.com/auth/login` (запасные `api-e` / `api-a`, TLS
   проверяется по встроенному набору сертификатов) и сохраняет только
   `user_id`; пароль не сохраняется.
2. **Выбор станций.** Страница показывает станции EcoFlow, услышанные по
   Bluetooth рядом (модель, серийник, сигнал), сильные сверху. Отметьте не
   больше, чем блоков `ecoflow_ble` в прошивке, или введите серийник
   вручную. *Refresh list*, если станции нет в списке (включена ли она и
   рядом ли?).
3. **Save**, затем *Reboot now* (или *Exit setup* на устройстве).
   Устройство перезагружается в обычный режим: Wi-Fi выключен, станции
   находятся по BLE и авторизуются.

Чтобы потом сменить станции или аккаунт, снова войдите в режим настройки
(*Start setup* в отладочном интерфейсе или кнопка `ef_provision` с
`type: start_setup`, см. [Сущности](#сущности)).

## Конфигурация

```yaml
external_components:
  - source: github://kernelorg/esphome-ecoflow-ble@main
    components:
      - ecoflow_ble
      - ef_provision
  # или локальная копия:
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
  enable_on_boot: false        # его включает ef_provision при загрузке (оба режима)

wifi:
  enable_on_boot: false        # Wi-Fi только в режиме настройки; его включает ef_provision

improv_ble:                    # телефон передаёт сеть Wi-Fi по BLE
  authorizer: none
  next_url: "http://{{ip_address}}/"   # «Visit device» открывает страницу настройки

esp32_ble_tracker:
  scan_parameters:
    active: true
    continuous: false

ef_provision:
  id: ef_prov
  ecoflow_ble_id: unit1        # показывать на странице станции, найденные рядом
  # forget_wifi_on_setup: true # забывать сохранённый Wi-Fi при входе в настройку
  # auto_start: true           # выбрать режим при загрузке, включить BLE, сканировать

ble_client:                    # по одному на станцию
  - id: ef_client1
    mac_address: 00:00:00:00:00:00
    auto_connect: true
  - id: ef_client2
    mac_address: 00:00:00:00:00:00
    auto_connect: true

ecoflow_ble:                   # по одному на станцию
  - id: unit1
    ble_client_id: ef_client1
    slot: 0                    # 1-я станция, выбранная на странице настройки
  - id: unit2
    ble_client_id: ef_client2
    slot: 1                    # 2-я выбранная станция
```

### Параметры `ecoflow_ble`

| Параметр | По умолчанию | |
|---|---|---|
| `ble_client_id` | — | `ble_client` этой станции (его MAC задаётся автоматически, когда станция найдена) |
| `slot` | — | загружать из NVS серийник ячейки N (0 = первая выбранная станция) и общий `user_id` |
| `device_sn`, `user_id` | `""` | постоянные данные вместо `slot` (страница настройки не нужна) |
| `auto_connect` | `true` | подключаться, как только видна реклама станции |

### Параметры `ef_provision`

| Параметр | По умолчанию | |
|---|---|---|
| `ecoflow_ble_id` | — | любой `ecoflow_ble`: тогда страница настройки показывает станции рядом |
| `forget_wifi_on_setup` | `true` | стирать сохранённый Wi-Fi при входе в режим настройки, чтобы Improv всегда спрашивал сеть |
| `auto_start` | `true` | при загрузке: режим настройки, если ничего не сохранено или настройка запрошена, иначе обычный режим; включает BLE и запускает сканирование — `on_boot` не нужен |

Можно выбрать столько станций, сколько блоков `ecoflow_ble` (до 8). ESP32
по умолчанию держит 3 BLE-подключения (`esp32_ble: max_connections`, до 9),
и каждое подключение занимает память — реально 2–3 станции рядом с Wi-Fi и
дисплеем.

## Использование в своём конфиге

### Сущности

Обычный для ESPHome способ — для автоматизаций, веб-страницы или Home Assistant:

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
    type: battery_level          # см. таблицу ниже
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
| `switch` | `ecoflow_ble` | `output:` `ac`, `ac2`, `ac_hv`, `ac_lv`, `usb`, `dc` | переключает выход; состояние — настоящее, из телеметрии станции (Delta 3 / River 3 / Delta Pro 3). Пока станция не сообщила о выходе — и на Delta 2 / River 2 — это последняя отправленная команда (assumed state). При загрузке ничего не переключается |
| `select` | `ecoflow_ble` | — | встроенный свет: Off / Dim / Bright / SOS |
| `sensor` | `ecoflow_ble` | `rssi` (по умолчанию), `battery_level`, `input_power`, `output_power`, `ac_input_power`, `ac_output_power`, `dc_output_power`, `usb_c_output_power`, `usb_a_output_power`, `solar_input_power`, `charge_remaining`, `discharge_remaining`, `cell_temperature`, `charge_limit`, `discharge_limit` | RSSI (дБм) работает для всех моделей; остальное — из телеметрии: заряд %, мощность Вт (выходы — положительными числами), оставшееся время заряда / разряда в минутах, максимальная температура ячейки °C, пороги заряда / разряда % |
| `binary_sensor` | `ecoflow_ble` | `found`, `connected`, `authenticated`, `ac_plugged_in` | состояние связи; `ac_plugged_in` (телеметрия) — подключена зарядка от сети |
| `text_sensor` | `ecoflow_ble` | `model`, `serial`, `state`, `error`, `address`, `outputs` | данные станции и связи; `error` объясняет отказ |
| `button` | `ef_provision` | `start_setup`, `exit_setup`, `forget_wifi` | войти в режим настройки / выйти из него (обе с перезагрузкой); забыть сохранённый Wi-Fi |
| `binary_sensor` | `ef_provision` | — | включён в режиме настройки |
| `text_sensor` | `ef_provision` | `status`, `ip` | ход настройки; адрес страницы настройки |

`ecoflow_ble_id` / `ef_provision_id` можно не указывать, если компонент
один. Команда, отправленная без авторизации или для выхода, которого нет у
модели, не отправляется и пишется в лог как предупреждение.

Home Assistant подключается по Wi-Fi, а эта конфигурация в обычном режиме
держит Wi-Fi выключенным (`wifi: enable_on_boot: false`); с Wi-Fi в обычном
режиме BLE и Wi-Fi делят радио — пока не проверялось.

### Действия

```yaml
- ecoflow_ble.set_output:
    id: unit1
    output: ac          # ac | ac2 | ac_hv | ac_lv | usb | dc
    state: true         # можно шаблоном
- ecoflow_ble.set_light:
    id: unit2
    mode: bright        # off | dim | bright | sos (River 3 Plus)
```

Пример — кнопка BOOT: короткое нажатие переключает AC, нажатие на 3 с запускает настройку:

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

### API из лямбд — `ecoflow_ble`

| Метод | |
|---|---|
| `set_output(OUT_AC / OUT_AC2 / OUT_AC_HV / OUT_AC_LV / OUT_USB / OUT_DC, bool)` | переключить выход; `false`, если нет авторизации или у модели нет такого выхода |
| `supports(output)`, `outputs_str()` | выходы модели, например `"ac, dc"` |
| `set_light(LIGHT_OFF / LIGHT_DIM / LIGHT_BRIGHT / LIGHT_SOS)`, `supports_light()` | встроенный свет |
| `model_name()`, `is_supported()`, `family()` | `"River 3 Plus"`, … или `"Unknown (XXXX)"` |
| `is_found()`, `address()`, `rssi()`, `adv_name()`, `encrypt_type()` | обнаружение |
| `is_connected()`, `is_authenticated()`, `state_name()`, `last_error()` | состояние связи; `last_error()` объясняет отказ (неверный ключ, не привязана, …) |
| `device_sn()`, `slot()`, `set_device_sn(sn)`, `set_user_id(id)` | учётные данные |
| `send_config(field, bool)`, `send_config_varint(field, value)` | Delta 3 / River 3 / Delta Pro 3: сырое поле `ConfigWrite` |
| `has_telemetry()`, `telemetry_age_ms()`, `battery_level()`, `input_power()`, `output_power()`, `ac_output_power()`, `dc_output_power()`, … `output_state(OUT_AC)`, `light_mode()`, `telemetry_raw(field)` | телеметрия (`optional<>`: пусто, пока не пришло); номера полей `DisplayPropertyUpload` |
| `EcoFlowBLE::instance_count()`, `EcoFlowBLE::instance(i)` | все станции в конфиге (например, для интерфейса) |
| `set_collect_nearby(bool)`, `nearby()` → `{serial, model, rssi}` | станции EcoFlow, услышанные рядом, сильные первыми |
| `EcoFlowBLE::model_for_serial(sn)` | название модели для любого серийника |

Для имён `OUT_*` / `LIGHT_*` пишите в лямбде
`using namespace esphome::ecoflow_ble;`.

### API из лямбд — `ef_provision`

| Метод | |
|---|---|
| `set_active(true)` | запустить режим настройки — только с `auto_start: false`, когда режим вы выбираете сами (`ef_nvs::want_provisioning()`) |
| `start_setup(forget_wifi = true)` | перезагрузиться в режим настройки (например, по долгому нажатию кнопки) |
| `exit_setup()` | выйти из режима настройки с перезагрузкой (обычный режим, если данные сохранены) |
| `forget_wifi()` | стереть сеть Wi-Fi, сохранённую через Improv |
| `is_active()`, `step()`, `ip()`, `wifi_up()`, `message()` | ход настройки для дисплея (`ProvStep::WAIT_WIFI / AWAIT_LOGIN / AWAIT_DEVICE / DONE`) |

### Добавление станции без страницы настройки

Страница настройки только записывает NVS; те же ключи можно записать
самому — например, из своего интерфейса — и перезагрузиться:

```yaml
- lambda: |-
    ef_nvs::set_str("user_id", "1234567890123456789");  // из входа в EcoFlow
    ef_nvs::set_str("serial",  "R631XXXXXXXXXXXX");     // ячейка 0
    ef_nvs::set_str("serial1", "R651XXXXXXXXXXXX");     // ячейка 1 ("serial<N>")
    ef_nvs::set_bool("force_prov", false);
- lambda: 'App.safe_reboot();'
```

Или вообще без NVS: `ecoflow_ble: { device_sn: "...", user_id: "..." }`.
`user_id` выводится в лог после успешного входа на странице настройки
(`login OK ... userId=...`).

### Веб-форма настройки (HTTP)

Отдаётся `ef_provision` на порту 80 в режиме настройки,
`application/x-www-form-urlencoded`:

| Запрос | Поля | |
|---|---|---|
| `GET /` | — | текущий шаг (вход, выбор станций или готово) |
| `POST /login` | `email`, `password` | вход в облако; при успехе — страница выбора станций |
| `POST /serial` | `sn` (повторить для каждой отмеченной станции), `serial` (необязательный введённый серийник) | сохраняет станции в ячейки 0, 1, …; очищает лишние |
| `POST /reboot` | — | перезагрузка |

Например: `curl -d "sn=R631XXXXXXXXXXXX&sn=R651XXXXXXXXXXXX" http://<ip>/serial`
(после входа в той же сессии настройки).

## Необязательный отладочный интерфейс (LVGL)

[`ui/lvgl_debug.yaml`](ui/lvgl_debug.yaml) — пакет для небольшого
горизонтального дисплея (240×135) с двумя кнопками; для обычной работы он не
нужен.

```yaml
packages:
  ecoflow_ui: !include
    file: <path>/esphome-ecoflow-ble/ui/lvgl_debug.yaml
    vars:
      provision_id: ef_prov
      display_id: lcd            # дисплей, на котором работает LVGL
      rotation: "90"             # горизонтальный поворот без переворота
      key_left: btn_left         # binary_sensor: левая кнопка
      key_right: btn_right       # binary_sensor: правая кнопка
      backlight_id: backlight    # light, управляющий подсветкой
      deep_sleep_id: deep_sleep_ctl
```

| Страница | |
|---|---|
| Status | станция N/M, модель, серийник, состояние связи или ошибка, RSSI, шифрование, выходы; `Unit` переключает станции, ⚙ открывает Settings |
| Control | кнопки AC / USB / DC выбранной станции (только те, что есть у модели), результат последней команды |
| Light | Off / Dim / Bright / SOS (модели со светом) |
| Setup | ход настройки, *Start setup* / *Exit setup* |
| Settings | *Flip screen* (180°), *Sleep after* 30 с / 1 мин / 3 мин / 5 мин |

Кнопки: левая — перевести фокус (красная рамка), правая — нажать; при
перевёрнутом экране они меняются местами и остаются «левой» и «правой» для
вас. Через выбранное время без нажатий подсветка гаснет и ESP32 уходит в
**deep sleep**; будит любая кнопка (само нажатие пробуждения дальше не
передаётся). В режиме настройки устройство не засыпает. Выводы
пробуждения задаёт конфиг устройства, см.
[`example/ecoflow_lvgl_tdisplay.yaml`](example/ecoflow_lvgl_tdisplay.yaml)
(LilyGO T-Display, две станции).

## Примеры

| Файл | |
|---|---|
| [`example/ecoflow.yaml`](example/ecoflow.yaml) | минимальный: одна станция, настройка через Improv, кнопка BOOT переключает AC |
| [`example/ecoflow_lvgl_tdisplay.yaml`](example/ecoflow_lvgl_tdisplay.yaml) | LilyGO T-Display: две станции + отладочный интерфейс + deep sleep |

Сборка из корня репозитория:

```bash
docker run --rm -v "$PWD":/config -w /config ghcr.io/esphome/esphome:2026.10.1 compile example/ecoflow.yaml
```

## Детали протокола

- Характеристики ищутся по известным UUID, а если их нет — по свойствам (WRITE + NOTIFY): у Delta 3 собственный UART-сервис.
- Реклама EcoFlow: company ID `0xB5B5`, серийник в байтах 1–16, тип шифрования в битах 3–5 байта 22.
- Шифрование типа 7: ECDH secp160r1 → getKeyInfo → ключ сессии `md5(EF_KEYDATA[pos:pos+16] + sRand)`, IV `md5(общий секрет)`; кадры 5A5A с AES-128-CBC/PKCS7. Тип 1: ключ `md5(серийник)`, IV `md5(серийник задом наперёд)`, открытый 5-байтный заголовок + тело AES с дополнением нулями. Тип 0: пакеты без шифрования.
- Авторизация: getAuthStatus, затем autoAuth с `md5(user_id + серийник)` в виде HEX заглавными; код ответа расшифровывается в `last_error()`.
- Delta 3 / River 3 / Delta Pro 3: `Packet(0x20, 0x02, 0xFE, 0x11, ConfigWrite, version 0x13)`; поля AC 76, AC2 377, AC HV 15, AC LV 16, USB 19, DC 18, свет (`cfg_led_mode`) 89.
- Delta 2 / River 2: версия пакета 2, `cmd_set 0x20`; AC `0x42` (адресат 0x05, у Delta 2 Max 0x04), USB `0x22` (0x02), DC `0x51` (0x05).
- Телеметрия (Delta 3 / River 3 / Delta Pro 3): после авторизации станция примерно раз в секунду присылает `src 0x02, cmd_set 0xFE, cmd_id 0x15` — protobuf `DisplayPropertyUpload` (полезная нагрузка с XOR по `seq[0]`), только изменившиеся поля; номера полей одинаковы во всех трёх семействах. Выход включён, когда два младших бита его `flow_info_*` равны `10` / `11`.
- Список устройств из облака по логину и паролю получить нельзя (нужны ключи разработчика), поэтому станции ищутся по BLE.

## Известные особенности

- Только **esp-idf**. Криптография — `esp_rom_md5_*` и аппаратный `esp_aes_*`.
- `wifi:` нужен с `enable_on_boot: false`; начиная с ESPHome 2026.10 Wi-Fi-драйвер тогда не инициализируется в обычном режиме. `improv_ble` оставляет в обычном режиме неактивный BLE GATT-сервер.
- Improv запускается, только если сеть Wi-Fi не сохранена; об этом заботится `forget_wifi_on_setup`. Сети из `wifi: networks:` не забываются — оставьте этот список пустым.
- `ef_provision` сам включает набор сертификатов и динамические буферы mbedTLS (TLS нужно ~35 КБ, пока в режиме настройки работают BLE, Wi-Fi и дисплей). Свободная память перед входом пишется в лог.
- `esp_http_client` и `esp_http_server` компонент добавляет в сборку сам.
- После добавления нового `.cpp` в компонент один раз нужен `esphome clean`.
- С `auto_start: false` учтите, что `ble.enable` на esp-idf асинхронный: задайте непрерывное сканирование до него, а не запускайте сканирование сразу после.
- Телеметрия разбирается для семейств Delta 3 / River 3 / Delta Pro 3; Delta 2 / River 2 используют другой формат и пока не разбираются. На запрос времени от станции ответа нет (без Wi-Fi у ESP32 нет часов), поэтому значения, которым оно нужно, могут отсутствовать.

## Лицензии

- Порт протокола основан на [rabits/ha-ef-ble](https://github.com/rabits/ha-ef-ble) — Apache-2.0, см. [`LICENSE.ha-ef-ble`](LICENSE.ha-ef-ble).
- `uECC.*` — [micro-ecc](https://github.com/kmackay/micro-ecc), © Kenneth MacKay, BSD 2-clause.
