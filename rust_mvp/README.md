# ESP32-C3 Button ADC MVP

Minimal Rust MVP for the current firmware pin contract in `PINS.md`.

- Pins:
  - `GPIO1` (`ADC1_CH1`) = front-button ladder channel (Back/Confirm/Left/Right).
  - `GPIO2` (`ADC1_CH2`) = front-button ladder channel (Up/Down).
  - `GPIO3` = power button input.
  - `GPIO20` = USB detect input.
- Pin mode setup that works on this board:
  - `GPIO1`: `INPUT` + explicit `PULLDOWN`.
  - `GPIO2`: `INPUT` + explicit `PULLDOWN`.
  - `GPIO3`: `INPUT` + explicit `PULLDOWN` (board-stable mode).
  - `GPIO20`: `INPUT` + explicit `PULLDOWN`.
- ADC is read through direct APB_SARADC onetime bypass by default:
  - No `ADC_ATTEN` environment variable is used anymore.
  - Attenuation bits written directly as `0x03` (`12dB` request).
  - This bypasses the default esp-hal oneshot path and reads `APB_SARADC::sar1data_status().saradc1_data` raw.
- Reads each button at 1 Hz and prints only decoded button names:
  - `button=Back|Confirm|Left|Right|Up|Down|none`
- Button map (GPIO1 order then GPIO2 order):
  - `GPIO1`: Back -> Confirm -> Left -> Right
  - `GPIO2`: Up -> Down
- Raw register calibration in this experiment build (no post-mask/truncation):
  - `GPIO1` thresholds (inclusive upper bound, exclusive lower bound): `[11400, 10280, 9800, 8400, i32::MIN]`
  - `GPIO2` thresholds: `[19300, 17200, i32::MIN]`
- Observed raw samples in this board validation:
  - Idle: `GPIO1≈11550`, `GPIO2≈19730`
  - Back: `GPIO1≈10811`
  - Confirm: `GPIO1≈10266`
  - Left: `GPIO1≈9433`
  - Right: `GPIO1≈8198`
  - Down: `GPIO2≈16390`
  - Up: `GPIO2≈18142`

User validation on-device confirmed this order: `Back -> Confirm -> Left -> Right -> Down -> Up`.

## Current board-mapped behavior

- Stable runtime settings recorded above are now in the loop:
  - fixed combo name: `g1-down-g2-down-g3-down-g20-down`
  - one fixed settle delay: `SAMPLE_SETTLE_DELAY_US = 1000`

- Expected output format:
  - `button=<Button>`

## Build and test (macOS ARM)

Run host tests against macOS ARM target:

- `cargo test --locked --target aarch64-apple-darwin`

## Build and flash (ESP32-C3)

- `cargo build --locked --target riscv32imc-unknown-none-elf`
- `cargo run --locked --target riscv32imc-unknown-none-elf` (uses `espflash --monitor` via `.cargo/config.toml`)

## Notes

- `.cargo/config.toml` is configured with the required linker script and frame-pointer flag for ESP32-C3 builds.
