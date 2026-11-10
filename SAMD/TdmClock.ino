/*
  Frame-sync generator for the TDM8-bus of the X-Cards using the PIO of the RP2040.

  The bit-clock (12.288 MHz) comes from an external oscillator and is connected to TDM_BCLK_PIN.
  One frame consists of 8 slots x 32 bit = 256 bit-clocks (48 kHz). The state-machine counts the
  rising edges of BCLK and toggles the frame-sync every 128 edges (50% duty-cycle). Like the former
  FPGA-design the frame-sync changes right after the rising edge of the bit-clock.

  PIO-program (sideset = frame-sync, "wait pin 0" = BCLK, y = 127):
   0 H0: mov x, y          side 1   ; frame-sync high-phase
   1 H1: wait 0 pin 0      side 1
   2 H2: jmp x-- H4        side 1   ; more edges to count?
   3 H3: wait 1 pin 0      side 1   ; last edge of high-phase -> continue with L0
   4 L0: mov x, y          side 0   ; frame-sync low-phase (this instruction toggles the pin)
   5 L1: wait 0 pin 0      side 0
   6 L2: jmp x-- L4        side 0
   7 L3: wait 1 pin 0      side 0   ; last edge of low-phase -> wrap to H0
   8 H4: wait 1 pin 0      side 1
   9 H5: jmp H1            side 1
  10 L4: wait 1 pin 0      side 0
  11 L5: jmp L1            side 0
*/

#include <hardware/pio.h>
#include <hardware/pio_instructions.h>

bool startTdmFrameSync() {
  const uint16_t instr[12] = {
    (uint16_t)(pio_encode_mov(pio_x, pio_y)  | pio_encode_sideset(1, 1)),
    (uint16_t)(pio_encode_wait_pin(false, 0) | pio_encode_sideset(1, 1)),
    (uint16_t)(pio_encode_jmp_x_dec(8)       | pio_encode_sideset(1, 1)),
    (uint16_t)(pio_encode_wait_pin(true, 0)  | pio_encode_sideset(1, 1)),
    (uint16_t)(pio_encode_mov(pio_x, pio_y)  | pio_encode_sideset(1, 0)),
    (uint16_t)(pio_encode_wait_pin(false, 0) | pio_encode_sideset(1, 0)),
    (uint16_t)(pio_encode_jmp_x_dec(10)      | pio_encode_sideset(1, 0)),
    (uint16_t)(pio_encode_wait_pin(true, 0)  | pio_encode_sideset(1, 0)),
    (uint16_t)(pio_encode_wait_pin(true, 0)  | pio_encode_sideset(1, 1)),
    (uint16_t)(pio_encode_jmp(1)             | pio_encode_sideset(1, 1)),
    (uint16_t)(pio_encode_wait_pin(true, 0)  | pio_encode_sideset(1, 0)),
    (uint16_t)(pio_encode_jmp(5)             | pio_encode_sideset(1, 0)),
  };
  pio_program_t program = {};
  program.instructions = instr;
  program.length = 12;
  program.origin = -1;

  PIO pio;
  uint sm;
  uint offset;
  // pio_add_program() relocates the jump-targets by the load-offset
  if (!pio_claim_free_sm_and_add_program_for_gpio_range(&program, &pio, &sm, &offset, TDM_FSYNC_PIN, 1, true)) {
    return false;
  }

  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_in_pins(&c, TDM_BCLK_PIN);
  sm_config_set_sideset(&c, 1, false, false);
  sm_config_set_sideset_pins(&c, TDM_FSYNC_PIN);
  sm_config_set_wrap(&c, offset + 0, offset + 7);
  sm_config_set_clkdiv(&c, 1.0f); // run at full system clock to keep delay to BCLK as small as possible

  pio_gpio_init(pio, TDM_BCLK_PIN);
  pio_gpio_init(pio, TDM_FSYNC_PIN);
  pio_sm_set_consecutive_pindirs(pio, sm, TDM_BCLK_PIN, 1, false);
  pio_sm_set_consecutive_pindirs(pio, sm, TDM_FSYNC_PIN, 1, true);
  pio_sm_init(pio, sm, offset, &c);

  // y = 127: the counter counts 127 edges in the loop plus the final edge = 128 edges per half-frame
  pio_sm_put_blocking(pio, sm, 127);
  pio_sm_exec(pio, sm, pio_encode_pull(false, false));
  pio_sm_exec(pio, sm, pio_encode_mov(pio_y, pio_osr));

  pio_sm_set_enabled(pio, sm, true);
  return true;
}

// Measure the frequency of the bit-clock on TDM_BCLK_PIN by counting rising edges with a second
// state-machine for 10 ms. Returns the frequency in Hz or 0 if there are no edges.
uint32_t measureBclkHz() {
  const uint16_t instr[3] = {
    (uint16_t)pio_encode_wait_pin(false, 0),
    (uint16_t)pio_encode_wait_pin(true, 0),
    (uint16_t)pio_encode_jmp_x_dec(0),       // x counts down once per rising edge
  };
  pio_program_t program = {};
  program.instructions = instr;
  program.length = 3;
  program.origin = -1;

  PIO pio;
  uint sm;
  uint offset;
  if (!pio_claim_free_sm_and_add_program(&program, &pio, &sm, &offset)) {
    return 0;
  }

  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_in_pins(&c, TDM_BCLK_PIN);
  sm_config_set_wrap(&c, offset, offset + 2);
  sm_config_set_clkdiv(&c, 1.0f);
  pio_sm_init(pio, sm, offset, &c);
  pio_sm_exec(pio, sm, pio_encode_mov_not(pio_x, pio_null)); // x = 0xFFFFFFFF

  uint32_t t0 = micros();
  pio_sm_set_enabled(pio, sm, true);
  delay(10);
  pio_sm_set_enabled(pio, sm, false);
  uint32_t dt = micros() - t0;

  pio_sm_exec(pio, sm, pio_encode_mov(pio_isr, pio_x));
  pio_sm_exec(pio, sm, pio_encode_push(false, false));
  uint32_t edges = 0xFFFFFFFFu - pio_sm_get_blocking(pio, sm);
  pio_remove_program_and_unclaim_sm(&program, pio, sm, offset);

  return (uint32_t)(((uint64_t)edges * 1000000ULL) / dt);
}

// Check the 12.288 MHz oscillator and print the result. Returns true if a clock signal is present.
// A frequency deviating by more than 1% is only reported, it does not fail the check.
bool checkBclk() {
  const uint32_t expectedHz = 12288000;
  uint32_t hz = measureBclkHz();

  Serial.print(F("   BCLK on GP"));
  Serial.print(TDM_BCLK_PIN);
  Serial.print(F(": "));
  Serial.print(hz);
  Serial.print(F(" Hz - "));

  if (hz == 0) {
    Serial.print(F("NO CLOCK, pin is stuck "));
    Serial.print(digitalRead(TDM_BCLK_PIN) ? F("high") : F("low"));
    Serial.println(F(". Check oscillator soldering (VCC, GND, enable-pin, output to GP6)."));
    return false;
  }
  uint32_t diff = (hz > expectedHz) ? (hz - expectedHz) : (expectedHz - hz);
  if (diff > (expectedHz / 100)) {
    Serial.println(F("signal present, but frequency differs from 12288000 Hz"));
    return true; // a signal is enough for the check to pass
  }
  Serial.println(F("OK"));
  return true;
}
