# Component Test Suite

This test program validates all hardware components for the Smart Hydration Reminder System while maintaining the **exact same wiring** as the main project.

## 🔌 Wiring Configuration

All pins are identical to `sketch_jan11a`:

| Component | Pin | Notes |
|-----------|-----|-------|
| **HX711 Load Cell** | | |
| - DOUT | GPIO 32 | Data output |
| - SCK | GPIO 33 | Clock |
| **White LED** | GPIO 25 | Notification LED |
| **RGB LED** | | Common Anode |
| - Red | GPIO 27 | |
| - Green | GPIO 14 | |
| - Blue | GPIO 12 | |
| **Buzzer** | GPIO 26 | Active buzzer |
| **Button** | GPIO 13 | Snooze button (INPUT_PULLUP) |

## 🚀 How to Use

1. **Upload the sketch** to your ESP32
2. **Open Serial Monitor** at 115200 baud
3. **Select a test** by entering 1-6

## 📋 Test Menu

```
1 - Test White LED          (3 blinks)
2 - Test RGB LED            (Color cycle)
3 - Test Buzzer            (Beep patterns)
4 - Test Button            (Interactive)
5 - Test Load Cell         (Weight reading)
6 - Run Full Test          (All components)
0 - Show menu
```

## 🧪 Individual Tests

### Test 1: White LED
- Blinks 3 times
- Verifies notification LED functionality

### Test 2: RGB LED
- Cycles through 7 colors: Red → Green → Blue → Yellow → Cyan → Magenta → White
- Tests all color combinations
- Verifies common anode wiring

### Test 3: Buzzer
- 3 short beeps
- 1 long beep
- Tests different durations

### Test 4: Button
- Interactive test requiring 5 button presses
- 10-second timeout
- Visual feedback with LED and RGB
- Tests INPUT_PULLUP configuration

### Test 5: Load Cell (HX711)
Options:
- **T** - Tare (zero) the scale
- **R** - Read weight 10 times
- **C** - Calibration mode

#### Calibration Process:
1. Remove all weight
2. Scale tares automatically
3. Place known weight (e.g., 218g powerbank)
4. Enter the known weight
5. System calculates new calibration factor
6. Update `CALIBRATION_FACTOR` in code if needed

### Test 6: Full Test
- Runs tests 1-3 automatically
- Skips interactive tests (button)
- Shows current load cell reading

## 🔧 Troubleshooting

### HX711 Not Responding
Check:
- DOUT → Pin 32
- SCK → Pin 33  
- VCC → 3.3V (or 5V depending on module)
- GND → GND
- Red wire (E+) to load cell red
- Black wire (E-) to load cell black
- White wire (A-) to load cell white
- Green wire (A+) to load cell green

### RGB LED Not Working
- Verify **common anode** wiring (common pin to VCC)
- Check if colors are inverted (code already handles this)

### Button Not Responding
- Verify pull-up resistor (code uses `INPUT_PULLUP`)
- Button should connect pin 13 to GND when pressed

## 📝 Notes

- **Same calibration factor** as main project: `350.3`
- **Same RGB inversion logic** for common anode
- All timings match production values
- No WiFi/MQTT/BLE required for testing

## ✅ Expected Results

All tests should pass:
- ✓ White LED blinks clearly
- ✓ RGB shows all colors accurately
- ✓ Buzzer produces clear tones
- ✓ Button presses detected reliably
- ✓ Load cell reads stable weights

If any test fails, check the specific component's wiring before proceeding to the main sketch!
