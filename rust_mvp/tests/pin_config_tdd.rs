use esp32c3_button_adc_mvp::pin_config::{
    AtLeast, pin_setup_spec,
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
