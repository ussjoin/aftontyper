// From https://learn.adafruit.com/adafruit-feather-rp2040-with-usb-type-a-host/arduino-ide-setup
// Boards manager URLs in Settings: add https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
// Boards Manager: add Raspberry Pi Pico/RP2040/RP2350 by Earle F Philhower, III
// Tools -> Board -> RasPi Pico / RP2040 -> Adafruit Feather RP2040 USB Host

// From https://learn.adafruit.com/adafruit-feather-rp2040-with-usb-type-a-host/usb-host-device-info
// Sketch -> Include Library -> Manage Libraries: Install each of Adafruit TinyUSB, Pico PIO USB, Adafruit Thermal Printer Library
// Tools -> CPU Speed -> 120MHz
// Tools -> USB Stack -> Adafruit TinyUSB

// Much of the below shamelessly stolen from https://github.com/adafruit/Adafruit_TinyUSB_Arduino/blob/master/examples/DualRole/HID/hid_remapper/hid_remapper.ino
// All changes (c) 2024 Brendan O'Connor, licensed MIT for compatibility
// All that code's license:

/*********************************************************************
 Adafruit invests time and resources providing this open source code,
 please support Adafruit and open-source hardware by purchasing
 products from Adafruit!

 MIT license, check LICENSE for more information
 Copyright (c) 2019 Ha Thach for Adafruit Industries
 All text above, and the splash screen below must be included in
 any redistribution
*********************************************************************/

/* This example demonstrates use of both device and host, where
 * - Device run on native usb controller (roothub port0)
 * - Host depending on MCUs run on either:
 *   - rp2040: bit-banging 2 GPIOs with the help of Pico-PIO-USB library (roothub port1)
 *
 * Requirements:
 * - For rp2040:
 *   - [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB) library
 *   - 2 consecutive GPIOs: D+ is defined by PIN_USB_HOST_DP, D- = D+ +1
 *   - Provide VBus (5v) and GND for peripheral
 *   - CPU Speed must be either 120 or 240 Mhz. Selected via "Menu -> CPU Speed"
 */

// USBHost is defined in usbh_helper.h
#include "usbh_helper.h"
// Naturally, from the Adafruit Thermal Printer library.
#include "Adafruit_Thermal.h"

#include "SoftwareSerial.h"
#define TX_PIN 25 // Arduino transmit  YELLOW WIRE  labeled RX on printer
#define RX_PIN 24 // Arduino receive   GREEN WIRE   labeled TX on printer
SoftwareSerial mySerial(RX_PIN, TX_PIN); // Declare SoftwareSerial obj first
Adafruit_Thermal printer(&mySerial);     // Pass addr to printer constructor

void setup() {
  Serial.begin(115200);

  mySerial.begin(19200);  // Initialize SoftwareSerial for printer
  printer.begin();        // Init printer (same regardless of serial type)

#if defined(CFG_TUH_MAX3421) && CFG_TUH_MAX3421
  // init host stack on controller (rhport) 1
  // For rp2040: this is called in core1's setup1()
  USBHost.begin(1);
#endif

  Serial.println("Hackertyper");
}

#if defined(CFG_TUH_MAX3421) && CFG_TUH_MAX3421
//--------------------------------------------------------------------+
// Using Host shield MAX3421E controller
//--------------------------------------------------------------------+
void loop() {
  USBHost.task();
}

#elif defined(ARDUINO_ARCH_RP2040)
//--------------------------------------------------------------------+
// For RP2040 use both core0 for device stack, core1 for host stack
//--------------------------------------------------------------------+

void loop() {
  // nothing to do
}

//------------- Core1 -------------//
void setup1() {
  // configure pio-usb: defined in usbh_helper.h
  rp2040_configure_pio_usb();

  //Initialize the printer buffer
  clear_buffer();

  // run host stack on controller (rhport) 1
  // Note: For rp2040 pico-pio-usb, calling USBHost.begin() on core1 will have most of the
  // host bit-banging processing works done in core1 to free up core0 for other works
  USBHost.begin(1);
}

void loop1() {
  USBHost.task();
}
#endif

//--------------------------------------------------------------------+
// TinyUSB Host callbacks
//--------------------------------------------------------------------+
extern "C"
{
  // Invoked when device with hid interface is mounted
  // Report descriptor is also available for use.
  // tuh_hid_parse_report_descriptor() can be used to parse common/simple enough
  // descriptor. Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE,
  // it will be skipped therefore report_desc = NULL, desc_len = 0
  void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
    (void) desc_report;
    (void) desc_len;
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    Serial.printf("HID device address = %d, instance = %d is mounted\r\n", dev_addr, instance);
    Serial.printf("VID = %04x, PID = %04x\r\n", vid, pid);

    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
      Serial.printf("HID Keyboard\r\n");
      if (!tuh_hid_receive_report(dev_addr, instance)) {
        Serial.printf("Error: cannot request to receive report\r\n");
      }
    }
  }

  // Invoked when device with hid interface is un-mounted
  void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    Serial.printf("HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
  }

  // Key data: https://github.com/hathach/tinyusb/blob/master/src/class/hid/hid.h#L1112
  uint8_t const conv_table[128][2] =  { HID_KEYCODE_TO_ASCII };
  uint8_t const buffer_length = 15;
  char printing_buffer[buffer_length+1];
  char last_unshifted;
  uint8_t printing_index = 0;
  unsigned long last_time;

  void clear_buffer()
  {
    for (uint8_t i = 0; i < buffer_length+1; i++)
    {
      printing_buffer[i] = 0;
    }
    printing_index = 0;

    //Set up initial key debounce time
    last_time = micros();
    last_unshifted = '0';

    // Put printer to sleep (on initial or after a print)
    printer.sleep();
  }

  void wicked_printme()
  {
    printer.wake();       // MUST wake() before printing again, even if reset
    // Font options
    printer.setFont('A');
    printer.setSize('L');        // Set type size, accepts 'S', 'M', 'L'

    printer.println(printing_buffer);
    
    // Send it to Arduino IDE as well
    Serial.println(printing_buffer);
    clear_buffer();
  }

  void wicked_buffer(hid_keyboard_report_t const *original_report) {
    for (uint8_t i = 0; i < 6; i++) {
      // only add to buffer if not empty report i.e key released
      if (original_report->keycode[i] != 0) {
        char comparator = conv_table[original_report->keycode[i]][0];

        // TODO: Debouncing here would be nice (mostly around shift key)
        if (comparator == last_unshifted)
        {
          // https://docs.arduino.cc/language-reference/en/functions/time/micros/
          unsigned long the_time = micros();
          if (the_time < last_time || the_time < last_time + 100000)
          {
            // either the_time < last_time (time overflowed) or the delta is < 0.1s
            break; // ignore this key to debounce 
          }
        } 

        bool shift = false;
        if (original_report->modifier == KEYBOARD_MODIFIER_LEFTSHIFT || original_report->modifier == KEYBOARD_MODIFIER_RIGHTSHIFT)
        {
          shift = true;
        }
        char ch = shift ? conv_table[original_report->keycode[i]][1] : conv_table[original_report->keycode[i]][0];

        bool print_now = false;
        // Specialcase Return key
        if (ch == 0x0d) // Carriage Return
        {
          printing_buffer[printing_index] = 0;
          print_now = true;
        }
        else if (ch == 0x20 && printing_index == 0) // Space
        {
          //We don't want leading spaces on lines because they make it look weird.
          break;
        }
        else
        {
          printing_buffer[printing_index] = ch;
          last_unshifted = comparator;
          printing_index++;
          if (printing_index == buffer_length)
          {
            print_now = true;
          }
        }

        last_time = micros();
        last_unshifted = comparator;

        if (print_now)
        {
          wicked_printme();
        }
        break;
      }
    }
  }

  // Invoked when received report from device via interrupt endpoint
  void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    if (len != 8) {
      Serial.printf("report len = %u NOT 8, probably something wrong !!\r\n", len);
    } else {
      wicked_buffer((hid_keyboard_report_t const *) report);

    }

    // continue to request to receive report
    if (!tuh_hid_receive_report(dev_addr, instance)) {
      Serial.printf("Error: cannot request to receive report\r\n");
    }
  }

}
