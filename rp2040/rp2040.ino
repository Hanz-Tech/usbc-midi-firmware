#include <usb_midi_host.h>
#include "pio_usb_configuration.h"
#include <Arduino.h>
#include <MIDI.h>
#include "usb_host_wrapper.h"
#include "pio_usb.h"
#include "led_utils.h"
#include "midi_instances.h"

// Include the new Serial MIDI header
#include "serial_midi_handler.h"
#include "midi_filters.h"
#include "serial_utils.h"
#include "web_serial_config.h"
#include "config.h"

// USB Host configuration
#define HOST_PIN_DP   12   // Pin used as D+ for host, D- = D+ + 1
#include "EZ_USB_MIDI_HOST.h"

// Serial MIDI pins configuration - MOVED TO serial_midi.cpp
// int serialRxPin = 1;   // GPIO pin for Serial1 RX
// int serialTxPin = 0;   // GPIO pin for Serial1 TX

// USB MIDI device address (set by onMIDIconnect callback in usb_host_wrapper.cpp)
// Make sure this is defined (not static) in usb_host_wrapper.cpp so it's linkable
extern uint8_t midi_dev_addr;

bool isConnectedToComputer = false;

// USB Host object
Adafruit_USBH_Host USBHost;

// Create USB Device MIDI instance
// USB_D is now created by the macro in midi_instances.h

// Create Serial MIDI instance using Serial1 - MOVED TO serial_midi.cpp
// MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, SERIAL_M);

// USB Host MIDI
USING_NAMESPACE_MIDI
USING_NAMESPACE_EZ_USB_MIDI_HOST


// Instantiate MIDI host with custom settings
EZ_USB_MIDI_HOST<MyCustomSettings> myMidiHost;
EZ_USB_MIDI_HOST<MyCustomSettings>& midiHost = myMidiHost; // This reference should be fine


volatile bool core1_booting = true;
uint32_t timeout = 2000; // 2 seconds timeout

// Forward declarations for USB Host MIDI message handlers (remain the same)
void usbh_onNoteOffHandle(byte channel, byte note, byte velocity);
void usbh_onNoteOnHandle(byte channel, byte note, byte velocity);
void usbh_onPolyphonicAftertouchHandle(byte channel, byte note, byte amount);
void usbh_onControlChangeHandle(byte channel, byte controller, byte value);
void usbh_onProgramChangeHandle(byte channel, byte program);
void usbh_onAftertouchHandle(byte channel, byte value);
void usbh_onPitchBendHandle(byte channel, int value);
void usbh_onSysExHandle(byte * array, unsigned size);
void usbh_onMidiClockHandle();
void usbh_onMidiStartHandle();
void usbh_onMidiContinueHandle();
void usbh_onMidiStopHandle();

// C-style wrappers for usb_host_wrapper.cpp linkage
void onNoteOff(Channel channel, byte note, byte velocity) { usbh_onNoteOffHandle(channel, note, velocity); }
void onNoteOn(Channel channel, byte note, byte velocity) { usbh_onNoteOnHandle(channel, note, velocity); }
void onPolyphonicAftertouch(Channel channel, byte note, byte pressure) { usbh_onPolyphonicAftertouchHandle(channel, note, pressure); }
void onControlChange(Channel channel, byte control, byte value) { usbh_onControlChangeHandle(channel, control, value); }
void onProgramChange(Channel channel, byte program) { usbh_onProgramChangeHandle(channel, program); }
void onAftertouch(Channel channel, byte pressure) { usbh_onAftertouchHandle(channel, pressure); }
void onPitchBend(Channel channel, int bend) { usbh_onPitchBendHandle(channel, bend); }
void onSysEx(byte * array, unsigned size) { usbh_onSysExHandle(array, size); }
void onMidiClock() { usbh_onMidiClockHandle(); }
void onMidiStart() { usbh_onMidiStartHandle(); }
void onMidiContinue() { usbh_onMidiContinueHandle(); }
void onMidiStop() { usbh_onMidiStopHandle(); }

// USB Device MIDI message handlers (remain the same)
void usbd_onNoteOn(byte channel, byte note, byte velocity);
void usbd_onNoteOff(byte channel, byte note, byte velocity);
void usbd_onControlChange(byte channel, byte controller, byte value);
void usbd_onProgramChange(byte channel, byte program);
void usbd_onAftertouch(byte channel, byte pressure);
void usbd_onPitchBend(byte channel, int bend);
void usbd_onSysEx(byte * array, unsigned size);
void usbd_onClock();
void usbd_onStart();
void usbd_onContinue();
void usbd_onStop();

// Serial MIDI message handlers - REMOVED (defined in serial_midi.cpp)
// void serial_onNoteOn(byte channel, byte note, byte velocity);
// ... (remove all serial_on... declarations)

void setup() {
  // Set custom USB serial device name


  dualPrintln("DEBUG: Entered setup()"); // Core 0
  Serial2.println("DEBUG: Core0 start Serial2"); // Core 0
  // Configure LED pins
  pinMode(LED_IN_PIN, OUTPUT);
  pinMode(LED_OUT_PIN, OUTPUT);
  digitalWrite(LED_IN_PIN, LOW);
  digitalWrite(LED_OUT_PIN, LOW);
  initLEDs();

  // Configure Serial1 pins - MOVED TO setupSerialMidi()
  // Serial1.setRX(serialRxPin);
  // Serial1.setTX(serialTxPin);

  // Initialize USB MIDI device
  char serialstr[32] = "usbc-midi-0001";
  USBDevice.setSerialDescriptor(serialstr);
  USBDevice.setManufacturerDescriptor("XYZ MIDI Mfg");
  USBDevice.setProductDescriptor("XYZDevice ");

  usb_midi.begin();

  // Initialize USB MIDI device handlers
  USB_D.begin(MIDI_CHANNEL_OMNI);
  USB_D.setHandleNoteOn(usbd_onNoteOn);
  USB_D.setHandleNoteOff(usbd_onNoteOff);
  USB_D.setHandleControlChange(usbd_onControlChange);
  USB_D.setHandleProgramChange(usbd_onProgramChange);
  USB_D.setHandleAfterTouchChannel(usbd_onAftertouch);
  USB_D.setHandlePitchBend(usbd_onPitchBend);
  USB_D.setHandleSystemExclusive(usbd_onSysEx);
  USB_D.setHandleClock(usbd_onClock);
  USB_D.setHandleStart(usbd_onStart);
  USB_D.setHandleContinue(usbd_onContinue);
  USB_D.setHandleStop(usbd_onStop);

  // Initialize Serial MIDI - MOVED TO setupSerialMidi()
  // SERIAL_M.begin(MIDI_CHANNEL_OMNI);
  // SERIAL_M.setHandleNoteOn(serial_onNoteOn);
  // ... (remove all SERIAL_M.setHandle... calls)

  // Initialize debug serial
  Serial.begin(115200);
  Serial2.setRX(25);
  Serial2.setTX(24);
  Serial2.begin(115200);

  // Add a timeout for USB device mounting (2 seconds max wait)
  uint32_t startTime = millis();
  
  // Wait for device to be mounted, but with a timeout
  while(!TinyUSBDevice.mounted()) {
    delay(1);
    // Break the loop if timeout is reached
    if(millis() - startTime > timeout) {
      dualPrintln("USB device not mounted after timeout - likely connected to power bank");
      break;
    }
  }
  
  // Check if we're connected to a computer or just a power source
  isConnectedToComputer = TinyUSBDevice.mounted();
  
  dualPrintln("RP2040 USB MIDI Router - Main Sketch");
  if(isConnectedToComputer) {
    dualPrintln("Connected to computer - USB Device mode active");
  } else {
    dualPrintln("Connected to power source only - Running in standalone mode");
  }

  // Initialize MIDI filters
  setupMidiFilters();
  enableAllChannels();
  // Load persisted filter/channel config from EEPROM
  loadConfigFromEEPROM();
  

  // Call the setup function for the Serial MIDI module
  setupSerialMidi();

  rp2040.fifo.push(0);
  while(rp2040.fifo.pop() != 1){};
  USB_D.turnThruOff();
  dualPrintln("Core0 setup complete");
  dualPrintln("");
  blinkBothLEDs(4, 100);
}


// Main loop for core 0
void loop() {
  // Process USB Host MIDI
  usb_host_wrapper_task();

  // Process USB Device MIDI only if connected to a computer
  if (isConnectedToComputer) {
    USB_D.read();
  }

  // Process Serial MIDI
  loopSerialMidi(); 

  // Handle Web Serial config commands (JSON over USB CDC)
  processWebSerialConfig();

  // Handle LED indicators
  handleLEDs();
}

void setup1() {
  while(rp2040.fifo.pop() != 0){};
  if (!isConnectedToComputer) {
    // We're in standalone mode, don't wait for Serial
    dualPrintln("Core1 setup in standalone mode");
  } else {
    // Only wait briefly for Serial when connected to computer
    uint32_t startTime = millis();
    while(!Serial && (millis() - startTime < timeout)); // 2 second timeout
  }
  
  

  // Check for CPU frequency, must be multiple of 120Mhz for bit-banging USB
  uint32_t cpu_hz = clock_get_hz(clk_sys);
  if (cpu_hz != 120000000UL && cpu_hz != 240000000UL) {
    delay(2000);   // wait for native usb
    dualPrintf("Error: CPU Clock = %u, PIO USB require CPU clock must be multiple of 120 Mhz\r\n", cpu_hz);
    dualPrintf("Change your CPU Clock to either 120 or 240 Mhz in Menu->CPU Speed\r\n");
    while(1) delay(1);
  }

  // Configure PIO USB
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP;

  #if defined(ARDUINO_RASPBERRY_PI_PICO_W)
    /* Need to swap PIOs so PIO code from CYW43 PIO SPI driver will fit */
    pio_cfg.pio_rx_num = 0;
    pio_cfg.pio_tx_num = 1;
  #endif

  USBHost.configure_pio_usb(1, &pio_cfg);

  // Initialize USB Host MIDI
  myMidiHost.begin(&USBHost, 1, onMIDIconnect, onMIDIdisconnect);
  rp2040.fifo.push(1);
  dualPrintln("Core1 setup to run TinyUSB host with pio-usb");
  dualPrintln("");
}

// Shared variables for LED state - Remains the same
// static uint32_t inLedStartMs = 0; // Assuming these are handled in led_utils.cpp/h now
// static bool inLedActive = false;
// static uint32_t outLedStartMs = 0;
// static bool outLedActive = false;

// --- USB Host MIDI message handlers ---
// These forward messages to USB Device MIDI and Serial MIDI
// MODIFIED to use sendSerialMidi... functions

void usbh_onNoteOffHandle(byte channel, byte note, byte velocity) {
  // Channel filter
  if (!isChannelEnabled(channel)) return;
  // First check if this message type is filtered for USB Host
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_NOTE)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintf("USB Host: Note Off - Channel: %d, Note: %d, Velocity: %d\n", channel, note, velocity);
  
  // Forward to USB Device MIDI if not filtered and connected to computer
  if (isConnectedToComputer && !isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_NOTE)) {
    USB_D.sendNoteOff(note, velocity, channel);
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_NOTE)) {
    sendSerialMidiNoteOff(channel, note, velocity);
  }
  
  triggerUsbLED();
}

void usbh_onNoteOnHandle(byte channel, byte note, byte velocity) {
  // Channel filter
  if (!isChannelEnabled(channel)) return;
  // First check if this message type is filtered for USB Host
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_NOTE)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintf("USB Host: Note On - Channel: %d, Note: %d, Velocity: %d\n", channel, note, velocity);
  
  // Forward to USB Device MIDI if not filtered and connected to computer
  if (isConnectedToComputer && !isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_NOTE)) {
    USB_D.sendNoteOn(note, velocity, channel);
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_NOTE)) {
    sendSerialMidiNoteOn(channel, note, velocity);
  }
  
  triggerUsbLED();
}

void usbh_onPolyphonicAftertouchHandle(byte channel, byte note, byte amount) {
  // Channel filter
  if (!isChannelEnabled(channel)) return;
  // First check if this message type is filtered for USB Host
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_POLY_AFTERTOUCH)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintf("USB Host: Poly Aftertouch - Channel: %d, Note: %d, Amount: %d\n", channel, note, amount);
  
  // Forward to USB Device MIDI if not filtered and connected to computer
  if (isConnectedToComputer && !isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_POLY_AFTERTOUCH)) {
    USB_D.sendAfterTouch(note, amount, channel);
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_POLY_AFTERTOUCH)) {
    sendSerialMidiAfterTouch(channel, note, amount);
  }
  
  triggerUsbLED();
}

void usbh_onControlChangeHandle(byte channel, byte controller, byte value) {
  // Channel filter
  if (!isChannelEnabled(channel)) return;
  // First check if this message type is filtered for USB Host
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_CONTROL_CHANGE)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintf("USB Host: CC - Channel: %d, Controller: %d, Value: %d\n", channel, controller, value);
  
  // Forward to USB Device MIDI if not filtered and connected to computer
  if (isConnectedToComputer && !isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_CONTROL_CHANGE)) {
    USB_D.sendControlChange(controller, value, channel);
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_CONTROL_CHANGE)) {
    sendSerialMidiControlChange(channel, controller, value);
  }
  
  triggerUsbLED();
}

void usbh_onProgramChangeHandle(byte channel, byte program) {
  // Channel filter
  if (!isChannelEnabled(channel)) return;
  // First check if this message type is filtered for USB Host
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_PROGRAM_CHANGE)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintf("USB Host: Program Change - Channel: %d, Program: %d\n", channel, program);
  
  // Forward to USB Device MIDI if not filtered and connected to computer
  if (isConnectedToComputer && !isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_PROGRAM_CHANGE)) {
    USB_D.sendProgramChange(program, channel);
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_PROGRAM_CHANGE)) {
    sendSerialMidiProgramChange(channel, program);
  }
  
  triggerUsbLED();
}

void usbh_onAftertouchHandle(byte channel, byte value) { // Channel Aftertouch
  // Channel filter
  if (!isChannelEnabled(channel)) return;
  // First check if this message type is filtered for USB Host
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_CHANNEL_AFTERTOUCH)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintf("USB Host: Channel Aftertouch - Channel: %d, Value: %d\n", channel, value);
  
  // Forward to USB Device MIDI if not filtered and connected to computer
  if (isConnectedToComputer && !isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_CHANNEL_AFTERTOUCH)) {
    USB_D.sendAfterTouch(value, channel);
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_CHANNEL_AFTERTOUCH)) {
    sendSerialMidiAfterTouchChannel(channel, value);
  }
  
  triggerUsbLED();
}

void usbh_onPitchBendHandle(byte channel, int value) {
  // Channel filter
  if (!isChannelEnabled(channel)) return;
  // First check if this message type is filtered for USB Host
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_PITCH_BEND)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintf("USB Host: Pitch Bend - Channel: %d, Value: %d\n", channel, value);
  
  // Forward to USB Device MIDI if not filtered and connected to computer
  if (isConnectedToComputer && !isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_PITCH_BEND)) {
    USB_D.sendPitchBend(value, channel);
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_PITCH_BEND)) {
    sendSerialMidiPitchBend(channel, value);
  }
  
  triggerUsbLED();
}

void usbh_onSysExHandle(byte * array, unsigned size) {
  // First check if this message type is filtered for USB Host
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_SYSEX)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintf("USB Host: SysEx - Size: %d\n", size);
  
  // Forward to USB Device MIDI if not filtered and connected to computer
  if (isConnectedToComputer && !isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_SYSEX)) {
    USB_D.sendSysEx(size, array);
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_SYSEX)) {
    sendSerialMidiSysEx(size, array);
  }
  
  triggerUsbLED();
}

void usbh_onMidiClockHandle() {
  // First check if this message type is filtered for USB Host
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_REALTIME)) {
    return; // Don't process the message if it's filtered
  }
  
  // Avoid printing every clock message
  // Serial2.println("USB Host: MIDI Clock");
  
  // Forward to USB Device MIDI if not filtered and connected to computer
  if (isConnectedToComputer && !isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_REALTIME)) {
    USB_D.sendRealTime(midi::Clock);
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_REALTIME)) {
    sendSerialMidiRealTime(midi::Clock);
  }
  
  triggerUsbLED();
}

void usbh_onMidiStartHandle() {
  // First check if this message type is filtered for USB Host
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_REALTIME)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintln("USB Host: MIDI Start");
  
  // Forward to USB Device MIDI if not filtered and connected to computer
  if (isConnectedToComputer && !isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_REALTIME)) {
    USB_D.sendRealTime(midi::Start);
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_REALTIME)) {
    sendSerialMidiRealTime(midi::Start);
  }
  
  triggerUsbLED();
}

void usbh_onMidiContinueHandle() {
  // First check if this message type is filtered for USB Host
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_REALTIME)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintln("USB Host: MIDI Continue");
  
  // Forward to USB Device MIDI if not filtered and connected to computer
  if (isConnectedToComputer && !isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_REALTIME)) {
    USB_D.sendRealTime(midi::Continue);
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_REALTIME)) {
    sendSerialMidiRealTime(midi::Continue);
  }
  
  triggerUsbLED();
}

void usbh_onMidiStopHandle() {
  // First check if this message type is filtered for USB Host
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_REALTIME)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintln("USB Host: MIDI Stop");
  
  // Forward to USB Device MIDI if not filtered and connected to computer
  if (isConnectedToComputer && !isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_REALTIME)) {
    USB_D.sendRealTime(midi::Stop);
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_REALTIME)) {
    sendSerialMidiRealTime(midi::Stop);
  }
  
  triggerUsbLED();
}

// --- USB Device MIDI message handlers ---
// These forward messages to USB Host MIDI and Serial MIDI
// MODIFIED to use sendSerialMidi... functions

void usbd_onNoteOn(byte channel, byte note, byte velocity) {
  // Channel filter
  if (!isChannelEnabled(channel)) return;
  // First check if this message type is filtered for USB Device
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_NOTE)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintf("USB Device: Note On - Channel: %d, Note: %d, Velocity: %d\n", channel, note, velocity);
  
  // Forward to USB Host MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_NOTE)) {
    auto intf = midiHost.getInterfaceFromDeviceAndCable(midi_dev_addr, 0);
    if (intf != nullptr) {
      intf->sendNoteOn(note, velocity, channel);
    }
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_NOTE)) {
    sendSerialMidiNoteOn(channel, note, velocity);
  }
  
  triggerUsbLED();
}

void usbd_onNoteOff(byte channel, byte note, byte velocity) {
  // Channel filter
  if (!isChannelEnabled(channel)) return;
  // First check if this message type is filtered for USB Device
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_NOTE)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintf("USB Device: Note Off - Channel: %d, Note: %d, Velocity: %d\n", channel, note, velocity);
  
  // Forward to USB Host MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_NOTE)) {
    auto intf = midiHost.getInterfaceFromDeviceAndCable(midi_dev_addr, 0);
    if (intf != nullptr) {
      intf->sendNoteOff(note, velocity, channel);
    }
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_NOTE)) {
    sendSerialMidiNoteOff(channel, note, velocity);
  }
  
  triggerUsbLED();
}

void usbd_onControlChange(byte channel, byte controller, byte value) {
  // First check if this message type is filtered for USB Device
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_CONTROL_CHANGE)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintf("USB Device: CC - Channel: %d, Controller: %d, Value: %d\n", channel, controller, value);
  
  // Forward to USB Host MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_CONTROL_CHANGE)) {
    auto intf = midiHost.getInterfaceFromDeviceAndCable(midi_dev_addr, 0);
    if (intf != nullptr) {
      intf->sendControlChange(controller, value, channel);
    }
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_CONTROL_CHANGE)) {
    sendSerialMidiControlChange(channel, controller, value);
  }
  
  triggerUsbLED();
}

void usbd_onProgramChange(byte channel, byte program) {
  // First check if this message type is filtered for USB Device
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_PROGRAM_CHANGE)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintf("USB Device: Program Change - Channel: %d, Program: %d\n", channel, program);
  
  // Forward to USB Host MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_PROGRAM_CHANGE)) {
    auto intf = midiHost.getInterfaceFromDeviceAndCable(midi_dev_addr, 0);
    if (intf != nullptr) {
      intf->sendProgramChange(program, channel);
    }
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_PROGRAM_CHANGE)) {
    sendSerialMidiProgramChange(channel, program);
  }
  
  triggerUsbLED();
}

void usbd_onAftertouch(byte channel, byte pressure) { // Channel Aftertouch
  // First check if this message type is filtered for USB Device
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_CHANNEL_AFTERTOUCH)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintf("USB Device: Channel Aftertouch - Channel: %d, Pressure: %d\n", channel, pressure);
  
  // Forward to USB Host MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_CHANNEL_AFTERTOUCH)) {
    auto intf = midiHost.getInterfaceFromDeviceAndCable(midi_dev_addr, 0);
    if (intf != nullptr) {
      intf->sendAfterTouch(pressure, channel);
    }
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_CHANNEL_AFTERTOUCH)) {
    sendSerialMidiAfterTouchChannel(channel, pressure);
  }
  
  triggerUsbLED();
}

void usbd_onPitchBend(byte channel, int bend) {
  // First check if this message type is filtered for USB Device
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_PITCH_BEND)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintf("USB Device: Pitch Bend - Channel: %d, Bend: %d\n", channel, bend);
  
  // Forward to USB Host MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_PITCH_BEND)) {
    auto intf = midiHost.getInterfaceFromDeviceAndCable(midi_dev_addr, 0);
    if (intf != nullptr) {
      intf->sendPitchBend(bend, channel);
    }
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_PITCH_BEND)) {
    sendSerialMidiPitchBend(channel, bend);
  }
  
  triggerUsbLED();
}

void usbd_onSysEx(byte * array, unsigned size) {
  // First check if this message type is filtered for USB Device
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_SYSEX)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintf("USB Device: SysEx - Size: %d\n", size);
  
  // Forward to USB Host MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_SYSEX)) {
    auto intf = midiHost.getInterfaceFromDeviceAndCable(midi_dev_addr, 0);
    if (intf != nullptr) {
      intf->sendSysEx(size, array);
    }
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_SYSEX)) {
    sendSerialMidiSysEx(size, array);
  }
  
  triggerUsbLED();
}

// USB Device MIDI real-time message handlers
void usbd_onClock() {
  // First check if this message type is filtered for USB Device
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_REALTIME)) {
    return; // Don't process the message if it's filtered
  }
  
  // Avoid printing every clock message
  // Serial2.println("USB Device: MIDI Clock");
  
  // Forward to USB Host MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_REALTIME)) {
    auto intf = midiHost.getInterfaceFromDeviceAndCable(midi_dev_addr, 0);
    if (intf != nullptr) {
      intf->sendRealTime(midi::Clock);
    }
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_REALTIME)) {
    sendSerialMidiRealTime(midi::Clock);
  }
  
  // triggerUsbLED(); 
}

void usbd_onStart() {
  // First check if this message type is filtered for USB Device
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_REALTIME)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintln("USB Device: MIDI Start");
  
  // Forward to USB Host MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_REALTIME)) {
    auto intf = midiHost.getInterfaceFromDeviceAndCable(midi_dev_addr, 0);
    if (intf != nullptr) {
      intf->sendRealTime(midi::Start);
    }
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_REALTIME)) {
    sendSerialMidiRealTime(midi::Start);
  }
  
  triggerUsbLED();
}

void usbd_onContinue() {
  // First check if this message type is filtered for USB Device
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_REALTIME)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintln("USB Device: MIDI Continue");
  
  // Forward to USB Host MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_REALTIME)) {
    auto intf = midiHost.getInterfaceFromDeviceAndCable(midi_dev_addr, 0);
    if (intf != nullptr) {
      intf->sendRealTime(midi::Continue);
    }
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_REALTIME)) {
    sendSerialMidiRealTime(midi::Continue);
  }
  
  triggerUsbLED();
}

void usbd_onStop() {
  // First check if this message type is filtered for USB Device
  if (isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_DEVICE, (MidiMsgType)MIDI_MSG_REALTIME)) {
    return; // Don't process the message if it's filtered
  }
  
  dualPrintln("USB Device: MIDI Stop");
  
  // Forward to USB Host MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_USB_HOST, (MidiMsgType)MIDI_MSG_REALTIME)) {
    auto intf = midiHost.getInterfaceFromDeviceAndCable(midi_dev_addr, 0);
    if (intf != nullptr) {
      intf->sendRealTime(midi::Stop);
    }
  }
  
  // Forward to Serial MIDI if not filtered
  if (!isMidiFiltered((MidiInterfaceType)MIDI_INTERFACE_SERIAL, (MidiMsgType)MIDI_MSG_REALTIME)) {
    sendSerialMidiRealTime(midi::Stop);
  }
  
  triggerUsbLED();
}
