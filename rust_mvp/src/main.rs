#![cfg_attr(target_arch = "riscv32", no_std)]
#![cfg_attr(target_arch = "riscv32", no_main)]

#[cfg(not(target_arch = "riscv32"))]
fn main() {
    println!(
        "ESP32-C3 firmware target only: build with a riscv32 target and espflash to run on device."
    );
}

#[cfg(target_arch = "riscv32")]
use esp_hal::{
    analog::adc::{Adc, AdcConfig, Attenuation},
    gpio::{Input, InputConfig, Pull},
    time::{Duration, Instant},
    main,
};

#[cfg(target_arch = "riscv32")]
esp_bootloader_esp_idf::esp_app_desc!();

#[cfg(target_arch = "riscv32")]
use esp_println::println;

#[cfg(target_arch = "riscv32")]
use nb::block;

#[cfg(target_arch = "riscv32")]
use esp32c3_button_adc_mvp::pin_config::pin_setup_spec;

#[cfg(target_arch = "riscv32")]
#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    loop {}
}

#[cfg(target_arch = "riscv32")]
#[main]
fn main() -> ! {
    let peripherals = esp_hal::init(esp_hal::Config::default());
    let mut adc_config = AdcConfig::new();

    let button_adc_pins = pin_setup_spec();
    let mut adc1_pin_1 = adc_config.enable_pin(peripherals.GPIO1, Attenuation::_11dB);
    let mut adc1_pin_2 = adc_config.enable_pin(peripherals.GPIO2, Attenuation::_11dB);

    // Preserve the required power pin semantics from the firmware contract.
    // GPIO1 and GPIO2 are used as ADC channels with no pull configuration in this mode.
    let power_button =
        Input::new(peripherals.GPIO3, InputConfig::default().with_pull(Pull::Up));

    // Validate the required logical setup at startup.
    debug_assert_eq!(button_adc_pins.adc_pins(), &[1, 2]);
    debug_assert_eq!(button_adc_pins.adc_attenuation_db(), 11);
    debug_assert_eq!(button_adc_pins.power_button_pin(), 3);

    let mut adc1 = Adc::new(peripherals.ADC1, adc_config);
    let loop_delay = Duration::from_millis(1000);

    loop {
        let now = Instant::now();
        let pin_1: u16 = block!(adc1.read_oneshot(&mut adc1_pin_1)).unwrap_or_default();
        let pin_2: u16 = block!(adc1.read_oneshot(&mut adc1_pin_2)).unwrap_or_default();
        let power_state = if power_button.is_low() { "LOW" } else { "HIGH" };

        println!(
            "btn_adc1={} btn_adc2={} power={} (GPIO3 {})",
            pin_1, pin_2, power_state, button_adc_pins.power_button_pin()
        );

        while Instant::now() - now < loop_delay {}
    }
}
