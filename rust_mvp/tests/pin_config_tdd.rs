use esp32c3_button_adc_mvp::pin_config::{
    ADC_RANGES_1, ADC_RANGES_1_12DB, ADC_RANGES_2, ADC_RANGES_2_12DB, AtLeast, pin_setup_spec,
};

#[test]
fn adc_inputs_are_gpio_1_and_2_with_input_mode_and_11db_attenuation() {
    let cfg = pin_setup_spec();

    assert_eq!(cfg.adc_pins(), &[1, 2]);
    assert_eq!(cfg.adc_mode(), AtLeast::Input);
    assert_eq!(cfg.adc_attenuation_db(), 11);
}

#[test]
fn power_button_is_gpio_3_input_pullup_active_low() {
    let cfg = pin_setup_spec();

    assert_eq!(cfg.power_button_pin(), 3);
    assert_eq!(cfg.power_mode(), AtLeast::InputPullUp);
    assert!(cfg.power_button_active_low());
}

#[test]
fn adc_ranges_match_open_x4_input_manager() {
    assert_eq!(ADC_RANGES_1, [3800, 3100, 2090, 750, i32::MIN]);
    assert_eq!(ADC_RANGES_2, [3800, 1120, i32::MIN]);
}

#[test]
fn adc_ranges_match_board_tdd_12db_calibration_with_headroom() {
    assert_eq!(ADC_RANGES_1_12DB, [11400, 10280, 9800, 8400, i32::MIN]);
    assert_eq!(ADC_RANGES_2_12DB, [19300, 17200, i32::MIN]);
}
