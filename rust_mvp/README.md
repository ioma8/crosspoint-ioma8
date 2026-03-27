# ESP32-C3 Button ADC MVP

Minimal Rust MVP for the current firmware pin contract in `PINS.md`.

- GPIO1 and GPIO2 configured as ADC input pins.
- GPIO3 configured as `INPUT_PULLUP`.
- ADC attenuation set to `ADC_11db`.
- Reads GPIO1/GPIO2 ADC values and prints them with power-button state every second.

## Build and test (macOS ARM)

Run host tests against macOS ARM target:

- `cargo test --locked --target aarch64-apple-darwin`

## Build and flash (ESP32-C3)

- `cargo build --locked --target riscv32imc-unknown-none-elf`
- `cargo run --locked --target riscv32imc-unknown-none-elf` (uses `espflash --monitor` via `.cargo/config.toml`)

## Notes

- `.cargo/config.toml` is configured with the required linker script and frame-pointer flag for ESP32-C3 builds.
