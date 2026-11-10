/*
  Frame-sync generator for the TDM8-bus of the X-Cards using the PIO of the RP2040.

  The bit-clock (12.288 MHz) comes from an external oscillator and is connected to TDM_BCLK_PIN.
  One frame consists of 8 slots x 32 bit = 256 bit-clocks (48 kHz). The state-machine counts the
  edges of BCLK and toggles the frame-sync every 128 edges (50% duty-cycle). Like the former
  FPGA-design the frame-sync changes right after the rising edge of the bit-clock by default.

  For debugging the phase of the frame-sync can be changed at runtime (see Communication.ino):
  - setTdmFsyncFalling(): frame-sync changes after the falling edge of BCLK instead (half a bit-clock later)
  - shiftTdmFsync(n): one frame is made n bit-clocks longer, so all following frame-syncs come n bit-clocks later

  PIO-program (sideset = frame-sync, BCLK = in-pin 0, y = 127).
  "wait A" / "wait B" are wait-for-low / wait-for-high for rising-edge mode and swapped for falling-edge mode:
   0 H0:  mov x, null      side 1   ; frame-sync high-phase (this instruction toggles the pin)
   1 H0a: pull noblock     side 1   ; extra bit-clocks for this frame from FIFO (x = 0 if FIFO is empty)
   2 H0b: mov x, osr       side 1
   3 HE:  jmp !x HC        side 1
   4 HE1: wait A           side 1   ; insert x additional bit-clocks
   5 HE2: wait B           side 1
   6 HE3: jmp x-- HE       side 1
   7 HC:  mov x, y         side 1
   8 H1:  wait A           side 1
   9 H2:  jmp x-- H4       side 1   ; more edges to count?
  10 H3:  wait B           side 1   ; last edge of high-phase -> continue with L0
  11 L0:  mov x, y         side 0   ; frame-sync low-phase (this instruction toggles the pin)
  12 L1:  wait A           side 0
  13 L2:  jmp x-- L4       side 0
  14 L3:  wait B           side 0   ; last edge of low-phase -> wrap to H0
  15 H4:  wait B           side 1
  16 H5:  jmp H1           side 1
  17 L4:  wait B           side 0
  18 L5:  jmp L1           side 0
*/

#include <hardware/pio.h>
#include <hardware/pio_instructions.h>

#define TDM_FSYNC_PROGRAM_LEN 19

PIO tdmFsyncPio = nullptr;
uint tdmFsyncSm = 0;
uint tdmFsyncOffset = 0;
bool tdmFsyncFalling = false; // false: frame-sync changes after rising edge of BCLK

bool startTdmFrameSync() {
  // wait for BCLK low/high (rising-edge mode) or high/low (falling-edge mode)
  const uint16_t waitA = pio_encode_wait_pin(tdmFsyncFalling, 0);
  const uint16_t waitB = pio_encode_wait_pin(!tdmFsyncFalling, 0);
  const uint16_t instr[TDM_FSYNC_PROGRAM_LEN] = {
    (uint16_t)(pio_encode_mov(pio_x, pio_null) | pio_encode_sideset(1, 1)), // 0 H0
    (uint16_t)(pio_encode_pull(false, false)   | pio_encode_sideset(1, 1)), // 1 H0a
    (uint16_t)(pio_encode_mov(pio_x, pio_osr)  | pio_encode_sideset(1, 1)), // 2 H0b
    (uint16_t)(pio_encode_jmp_not_x(7)         | pio_encode_sideset(1, 1)), // 3 HE
    (uint16_t)(waitA                           | pio_encode_sideset(1, 1)), // 4 HE1
    (uint16_t)(waitB                           | pio_encode_sideset(1, 1)), // 5 HE2
    (uint16_t)(pio_encode_jmp_x_dec(3)         | pio_encode_sideset(1, 1)), // 6 HE3
    (uint16_t)(pio_encode_mov(pio_x, pio_y)    | pio_encode_sideset(1, 1)), // 7 HC
    (uint16_t)(waitA                           | pio_encode_sideset(1, 1)), // 8 H1
    (uint16_t)(pio_encode_jmp_x_dec(15)        | pio_encode_sideset(1, 1)), // 9 H2
    (uint16_t)(waitB                           | pio_encode_sideset(1, 1)), // 10 H3
    (uint16_t)(pio_encode_mov(pio_x, pio_y)    | pio_encode_sideset(1, 0)), // 11 L0
    (uint16_t)(waitA                           | pio_encode_sideset(1, 0)), // 12 L1
    (uint16_t)(pio_encode_jmp_x_dec(17)        | pio_encode_sideset(1, 0)), // 13 L2
    (uint16_t)(waitB                           | pio_encode_sideset(1, 0)), // 14 L3
    (uint16_t)(waitB                           | pio_encode_sideset(1, 1)), // 15 H4
    (uint16_t)(pio_encode_jmp(8)               | pio_encode_sideset(1, 1)), // 16 H5
    (uint16_t)(waitB                           | pio_encode_sideset(1, 0)), // 17 L4
    (uint16_t)(pio_encode_jmp(12)              | pio_encode_sideset(1, 0)), // 18 L5
  };
  pio_program_t program = {};
  program.instructions = instr;
  program.length = TDM_FSYNC_PROGRAM_LEN;
  program.origin = -1;

  // restart: remove the running program first
  if (tdmFsyncPio != nullptr) {
    pio_sm_set_enabled(tdmFsyncPio, tdmFsyncSm, false);
    pio_remove_program_and_unclaim_sm(&program, tdmFsyncPio, tdmFsyncSm, tdmFsyncOffset);
    tdmFsyncPio = nullptr;
  }

  PIO pio;
  uint sm;
  uint offset;
  // pio_add_program() relocates the jump-targets by the load-offset
  if (!pio_claim_free_sm_and_add_program_for_gpio_range(&program, &pio, &sm, &offset, TDM_FSYNC_PIN, 1, true)) {
    return false;
  }
  tdmFsyncPio = pio;
  tdmFsyncSm = sm;
  tdmFsyncOffset = offset;

  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_in_pins(&c, TDM_BCLK_PIN);
  sm_config_set_sideset(&c, 1, false, false);
  sm_config_set_sideset_pins(&c, TDM_FSYNC_PIN);
  sm_config_set_wrap(&c, offset + 0, offset + 14);
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

// Frame-sync changes after the falling instead of the rising edge of BCLK. Restarts the generator.
bool setTdmFsyncFalling(bool falling) {
  tdmFsyncFalling = falling;
  return startTdmFrameSync();
}

// Make one frame n bit-clocks longer; all following frame-syncs come n bit-clocks later.
// Shifting by 256-n is the same as shifting n bit-clocks earlier.
bool shiftTdmFsync(uint32_t n) {
  if ((tdmFsyncPio == nullptr) || pio_sm_is_tx_fifo_full(tdmFsyncPio, tdmFsyncSm)) {
    return false;
  }
  pio_sm_put(tdmFsyncPio, tdmFsyncSm, n % 256);
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
