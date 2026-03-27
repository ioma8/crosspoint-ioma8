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
    delay::Delay,
    gpio::{Input, InputConfig, Pull, RtcPinWithResistors},
    main,
    peripherals::APB_SARADC,
    time::{Duration, Instant},
};

#[cfg(target_arch = "riscv32")]
esp_bootloader_esp_idf::esp_app_desc!();

#[cfg(target_arch = "riscv32")]
use esp_println::println;

#[cfg(target_arch = "riscv32")]
use esp32c3_button_adc_mvp::pin_config::{
    ADC_RANGES_1, ADC_RANGES_1_12DB, ADC_RANGES_2, ADC_RANGES_2_12DB, BUTTON_ADC_PIN_1,
    BUTTON_ADC_PIN_2, POWER_BUTTON_PIN, pin_setup_spec,
};

#[cfg(target_arch = "riscv32")]
#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    loop {}
}

#[cfg(target_arch = "riscv32")]
#[derive(Debug, Clone, Copy)]
enum ReadMode {
    OnetimeBypass { raw_atten_bits: u8 },
}

#[cfg(target_arch = "riscv32")]
#[derive(Debug, Clone, Copy)]
#[allow(dead_code)]
enum ResistorState {
    Floating,
    PullUp,
    PullDown,
}

#[cfg(target_arch = "riscv32")]
impl ResistorState {
    fn as_label(&self) -> &'static str {
        match self {
            Self::Floating => "float",
            Self::PullUp => "pull_up",
            Self::PullDown => "pull_down",
        }
    }

    fn as_input_config(self) -> InputConfig {
        match self {
            Self::Floating => InputConfig::default(),
            Self::PullUp => InputConfig::default().with_pull(Pull::Up),
            Self::PullDown => InputConfig::default().with_pull(Pull::Down),
        }
    }
}

#[cfg(target_arch = "riscv32")]
#[derive(Debug, Clone, Copy)]
struct PinCombination {
    name: &'static str,
    adc_pin1: ResistorState,
    adc_pin2: ResistorState,
    power_pin3: ResistorState,
    usb_pin20: ResistorState,
}

#[cfg(target_arch = "riscv32")]
impl PinCombination {
    fn print_label(&self) {
        println!(
            "configuration: GPIO1: {} | GPIO2: {} | GPIO3: {} | GPIO20: {}",
            self.adc_pin1.as_label(),
            self.adc_pin2.as_label(),
            self.power_pin3.as_label(),
            self.usb_pin20.as_label(),
        )
    }
}

#[cfg(target_arch = "riscv32")]
const SAMPLE_SETTLE_DELAY_US: u32 = 1000;

#[cfg(target_arch = "riscv32")]
const ADC_ATTEN_BITS_12DB: u8 = 0x03;

#[cfg(target_arch = "riscv32")]
#[derive(Debug, Clone, Copy)]
enum Button {
    None,
    Back,
    Confirm,
    Left,
    Right,
    Up,
    Down,
}

#[cfg(target_arch = "riscv32")]
impl Button {
    fn as_str(self) -> &'static str {
        match self {
            Self::None => "none",
            Self::Back => "Back",
            Self::Confirm => "Confirm",
            Self::Left => "Left",
            Self::Right => "Right",
            Self::Up => "Up",
            Self::Down => "Down",
        }
    }
}

#[cfg(target_arch = "riscv32")]
const DEFAULT_READ_MODE: ReadMode = ReadMode::OnetimeBypass {
    raw_atten_bits: ADC_ATTEN_BITS_12DB,
};

#[cfg(target_arch = "riscv32")]
fn attenuation_label(mode: ReadMode) -> &'static str {
    match mode {
        ReadMode::OnetimeBypass { .. } => "12dB (onetime bypass)",
    }
}

#[cfg(target_arch = "riscv32")]
#[inline]
fn read_adc1_oneshot_raw(channel: u8, attenuation_bits: u8) -> u16 {
    // Direct one-shot path using APB_SARADC register block, bypassing esp-hal ADC API.
    let masked = attenuation_bits & 0x03;
    APB_SARADC::regs().onetime_sample().modify(|_, w| unsafe {
        w.saradc1_onetime_sample().set_bit();
        w.onetime_channel().bits(channel);
        w.onetime_atten().bits(masked)
    });
    APB_SARADC::regs()
        .onetime_sample()
        .modify(|_, w| w.onetime_start().set_bit());
    while !APB_SARADC::regs().int_raw().read().adc1_done().bit() {}

    let value = APB_SARADC::regs()
        .sar1data_status()
        .read()
        .saradc1_data()
        .bits() as u16;

    APB_SARADC::regs()
        .int_clr()
        .write(|w| w.adc1_done().clear_bit_by_one());
    APB_SARADC::regs()
        .onetime_sample()
        .modify(|_, w| w.onetime_start().clear_bit());

    value
}

#[cfg(target_arch = "riscv32")]
#[inline]
fn decode_button_from_ranges(raw: u16, decode_pin1: bool, atten_12db: bool) -> Option<Button> {
    let value = i32::from(raw);
    let (ranges, labels): (&[i32], &[Button]) = if decode_pin1 {
        if atten_12db {
            const LABELS: [Button; 4] =
                [Button::Back, Button::Confirm, Button::Left, Button::Right];
            (&ADC_RANGES_1_12DB, &LABELS)
        } else {
            const LABELS: [Button; 4] =
                [Button::Back, Button::Confirm, Button::Left, Button::Right];
            (&ADC_RANGES_1, &LABELS)
        }
    } else if atten_12db {
        return if value <= ADC_RANGES_2_12DB[1] {
            Some(Button::Down)
        } else if value <= ADC_RANGES_2_12DB[0] {
            Some(Button::Up)
        } else {
            None
        };
    } else {
        // Mapping mirrors legacy 11dB/12-bit-API behavior: Up, Down.
        if value <= ADC_RANGES_2[1] {
            return Some(Button::Down);
        }
        if value <= ADC_RANGES_2[0] && value > ADC_RANGES_2[1] {
            return Some(Button::Up);
        }
        return None;
    };

    // For pin1 paths, walk the descending range table.
    for i in 0..labels.len() {
        if ranges[i + 1] < value && value <= ranges[i] {
            return Some(labels[i]);
        }
    }
    None
}

#[cfg(target_arch = "riscv32")]
#[inline]
fn decode_button(
    pin1_norm: u16,
    pin2_norm: u16,
    atten_12db: bool,
) -> (Option<Button>, Option<Button>) {
    (
        decode_button_from_ranges(pin1_norm, true, atten_12db),
        decode_button_from_ranges(pin2_norm, false, atten_12db),
    )
}

#[cfg(target_arch = "riscv32")]
#[inline]
fn apply_rtc_pin_pull<Pin>(pin: &mut Pin, mode: ResistorState)
where
    Pin: RtcPinWithResistors,
{
    match mode {
        ResistorState::Floating => {
            pin.rtcio_pullup(false);
            pin.rtcio_pulldown(false);
        }
        ResistorState::PullUp => {
            pin.rtcio_pullup(true);
            pin.rtcio_pulldown(false);
        }
        ResistorState::PullDown => {
            pin.rtcio_pullup(false);
            pin.rtcio_pulldown(true);
        }
    }
}

#[cfg(target_arch = "riscv32")]
#[main]
fn main() -> ! {
    let peripherals = esp_hal::init(esp_hal::Config::default());
    let read_mode = DEFAULT_READ_MODE;
    let mut adc_config = AdcConfig::new();
    let delay = Delay::new();

    let button_adc_pins = pin_setup_spec();
    // Match full `HalGPIO::begin()` pin setup: USB detect input is configured before
    // ADC/button setup in the production firmware.
    let mut usb_detect = Input::new(peripherals.GPIO20, InputConfig::default());

    // Match C++ `InputManager::begin()` and clear potential RTC pull state on the ADC lines.
    peripherals.GPIO1.rtcio_pullup(false);
    peripherals.GPIO1.rtcio_pulldown(false);
    peripherals.GPIO2.rtcio_pullup(false);
    peripherals.GPIO2.rtcio_pulldown(false);

    let mut adc1_pin_1 = adc_config.enable_pin(peripherals.GPIO1, Attenuation::_11dB);
    let mut adc1_pin_2 = adc_config.enable_pin(peripherals.GPIO2, Attenuation::_11dB);

    peripherals.GPIO3.rtcio_pulldown(false);

    // Working wiring for this board: force tested pins to pull-down during ADC reads.
    let mut power_button = Input::new(
        peripherals.GPIO3,
        InputConfig::default().with_pull(Pull::Down),
    );
    let combo = PinCombination {
        name: "g1-down-g2-down-g3-down-g20-down",
        adc_pin1: ResistorState::PullDown,
        adc_pin2: ResistorState::PullDown,
        power_pin3: ResistorState::PullDown,
        usb_pin20: ResistorState::PullDown,
    };

    println!("setup: entering raw button decoder loop");
    println!(
        "adc_attn={} (direct register path)",
        attenuation_label(read_mode)
    );
    println!(
        "GPIO1 = ADC ADC1_CH1, GPIO2 = ADC ADC1_CH2, GPIO3 = power input, GPIO20 = USB detect"
    );
    println!("Printing: raw pin readings + 12-bit-decoded button mapping.");
    println!("Wire mappings (from C++ InputManager):");
    println!("  GPIO1: Back -> Confirm -> Left -> Right");
    println!("  GPIO2: Up -> Down");

    // Validate the required logical setup at startup.
    debug_assert_eq!(button_adc_pins.adc_pins(), &[1, 2]);
    debug_assert_eq!(button_adc_pins.power_button_pin(), 3);

    debug_assert_eq!(
        button_adc_pins.adc_pins(),
        &[BUTTON_ADC_PIN_1, BUTTON_ADC_PIN_2]
    );
    debug_assert_eq!(button_adc_pins.power_button_pin(), POWER_BUTTON_PIN);

    let _adc1 = Adc::new(peripherals.ADC1, adc_config);
    let loop_delay = Duration::from_secs(1);

    apply_rtc_pin_pull(&mut adc1_pin_1.pin, combo.adc_pin1);
    apply_rtc_pin_pull(&mut adc1_pin_2.pin, combo.adc_pin2);
    power_button.apply_config(&combo.power_pin3.as_input_config());
    usb_detect.apply_config(&combo.usb_pin20.as_input_config());

    println!();
    println!("=== EXPERIMENT: {} ===", combo.name);
    combo.print_label();
    println!("bypass: reading raw SAR1_DATA register directly");

    loop {
        let now = Instant::now();
        let ReadMode::OnetimeBypass { raw_atten_bits } = read_mode;
        let raw_1 = read_adc1_oneshot_raw(1, raw_atten_bits);

        delay.delay_micros(SAMPLE_SETTLE_DELAY_US);

        let raw_2 = read_adc1_oneshot_raw(2, raw_atten_bits);

        let (norm_1, norm_2) = (raw_1, raw_2);

        let atten_12db = true;
        let (pin1_button, pin2_button) = decode_button(norm_1, norm_2, atten_12db);
        let button = pin1_button.or(pin2_button).unwrap_or(Button::None);
        println!("button={}", button.as_str());

        while Instant::now() - now < loop_delay {}
    }
}
