#ifndef __DCSBIOS_H
#define __DCSBIOS_H

#ifndef NULL
#define NULL 0
#endif

#include <stdint.h>

#ifdef DCSBIOS_FOR_STM32
#include <itoa.h>
#endif

#include "internal/ExportStreamListener.h"
#include "internal/PollingInput.h"
#include "internal/Protocol.h"
#include "internal/Protocol.cpp.inc" // Needs to be a .cpp.inc to allow DCSBIOS_INCOMING_DATA_BUFFER_SIZE
#include "internal/Addresses.h"


#ifndef USART0_RX_vect
#define USART0_RX_vect USART_RX_vect
#define USART0_TX_vect USART_TX_vect
#define USART0_UDRE_vect USART_UDRE_vect
#endif

#ifndef PRR0
#define PRR0 PRR
#endif

namespace DcsBios {
	const unsigned char PIN_NC = 0xFF;
}

/*
The following is an ugly hack to work with the Arduino IDE's build system.
The DCS-BIOS Arduino Library is configured with #defines such as DCSBIOS_RS485_MASTER or DCSBIOS_RS485_SLAVE <address>.
To make sure these defines are visible when compiling the code, we can't put it into a separate translation unit.

Normally, those #defines would go in a separate "config.h" or you would use compiler flags. But since Arduino libraries
do not come with their own build system, we are just putting everything into the header file.
*/
#if defined(DCSBIOS_RS485_MASTER)
	#include "internal/DcsBiosNgRS485Master.h"
	#include "internal/DcsBiosNgRS485Master.cpp.inc"
#elif defined(DCSBIOS_RS485_SLAVE)
	#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega2560__) || defined(__AVR_ATmega1280__)
		#include "internal/UART.Mod/DcsBiosNgRS485Slave.h"
		#include "internal/UART.Mod/DcsBiosNgRS485Slave.cpp.inc"
	#else
		#include "internal/DcsBiosNgRS485Slave.h"
		#include "internal/DcsBiosNgRS485Slave.cpp.inc"
	#endif
#elif defined(DCSBIOS_ESP32_WIFI)
	#include "internal/ESP32WiFi/DcsBiosESP32WiFiSlave.h"
	#include "internal/ESP32WiFi/DcsBiosESP32WiFiSlave.cpp.inc"
#elif defined(DCSBIOS_IRQ_SERIAL)
	namespace DcsBios {
		ProtocolParser parser;

		ISR(USART0_RX_vect) {
			volatile uint8_t c = UDR0;
			parser.processCharISR(c);
		}
		
		void setup() {
			PRR0 &= ~(1<<PRUSART0);
			UBRR0H = 0;
			UBRR0L = 3; // 250000 bps
			UCSR0A = 0;
			UCSR0C = (1<<UCSZ00) | (1<<UCSZ01);
			
			UCSR0B = (1<<RXEN0) | (1<<TXEN0) | (1<<RXCIE0);
		}
		
		void loop() {
			PollingInput::pollInputs();
			ExportStreamListener::loopAll();
		}

		void resetAllStates() {
			PollingInput::resetAllStates();
		}

		static void usart_tx(const char* str) {
			const char* c = str;
			while (*c) {
				while(!(UCSR0A & (1<<UDRE0))); // wait until TX buffer is empty
				UDR0 = *c++; // write byte to TX buffer
			}
		}
		
		bool tryToSendDcsBiosMessage(const char* msg, const char* arg) {
			DcsBios::usart_tx(msg);
			DcsBios::usart_tx(" ");
			DcsBios::usart_tx(arg);
			DcsBios::usart_tx("\n");
			DcsBios::PollingInput::setMessageSentOrQueued();
			return true;
		}
	}
#elif defined(DCSBIOS_USBCOMPOSITE_STM32F1_SERIAL)
	#if !defined(ARDUINO_GENERIC_STM32F103C)
	#error "This USB Composite device creating Joystick+Serial on the same device is only supported on Arduino Bluepill STM32F103C with the Roger Clark/Maple Toolchain"
	#endif
	// This port allows for a Composite USB device - a single USB port that is multiple devices
	//         eg. Joystick + DCS-BIOS Serial
	//
	// Hardware requirement - Arduino Bluepill STM32F103C6 (32k flash) or STM32F103C8 (64k flash)
	//
	// Tl;Dr - You need to install the "STM32duino" _AND_ the Roger Clark/Maple "Arduino_STM32" target toolchains
	//
	//         You can install STM32duino by adding the following link in the ArduinoIDE->Preferences->Additional Board Manager URLs:
	//		   https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json
	// 		   Then in the Board Manager install "STM32duino".
	//
	//		   To install the Roger Clark/Maple Board package you need to download the ZIP from:
	//		   https://github.com/rogerclarkmelbourne/Arduino_STM32/archive/refs/tags/v1.0.0.zip
	//		   Unzip it into your Arduino sketch directory under ./hardware/Arduino_STM32 (note the drop of the _1.0.0 at the end)
	//         Then use this Fully Qualified Board Name (FQBN)
	//		   !TIP! Put this in a sketch.yaml next your sketch to automate it.
	//		   default_fqbn: Arduino_STM32:STM32F1:genericSTM32F103C:upload_method=STLinkMethod
	//
	// This makes it possible to create a device that has Joystick-only (eg flight stick controls) elements that DCS-BIOS
	// doesn't support as well as DCS-BIOS inputs that are not supported over Joystick (eg analog wheel brake) config in DCS.
	// You can also have DCS outputs! So indicators that don't necessarily match the button state can be implemented.
	#include <USBComposite.h>
	namespace DcsBios {
		ProtocolParser parser;
		void setup() {
		}
		void loop() {
			while (CompositeSerial.available()) {
				parser.processChar(CompositeSerial.read());
			}
			PollingInput::pollInputs();
			ExportStreamListener::loopAll();			
		}
		bool tryToSendDcsBiosMessage(const char* msg, const char* arg) {
			// Collapsed to a single line so it is tranmitted as a single USB packet to reduce thrashing
			char line[128];
			int n = snprintf(line, sizeof(line), "%s %s\n", msg, arg);
			CompositeSerial.write((uint8_t*)line, n);
			DcsBios::PollingInput::setMessageSentOrQueued();
			return true;
		}
		void resetAllStates() {
			PollingInput::resetAllStates();
		}
	}
#elif defined(DCSBIOS_DEFAULT_SERIAL)
	namespace DcsBios {
		ProtocolParser parser;
		void setup() {
			Serial.begin(250000);
		}
		void loop() {
			while (Serial.available()) {
				parser.processChar(Serial.read());
			}
			PollingInput::pollInputs();
			ExportStreamListener::loopAll();			
		}
		bool tryToSendDcsBiosMessage(const char* msg, const char* arg) {
			Serial.write(msg); Serial.write(' '); Serial.write(arg); Serial.write('\n');
			DcsBios::PollingInput::setMessageSentOrQueued();
			return true;
		}
		void resetAllStates() {
			PollingInput::resetAllStates();
		}
	}
#else
#error You must define one of DCSBIOS_DEFAULT_SERIAL, DCSBIOS_USBCOMPOSITE_STM32F1_SERIAL, DCSBIOS_IRQ_SERIAL, DCSBIOS_ESP32_WIFI, DCSBIOS_RS485_MASTER, DCSBIOS_RS485_SLAVE
#endif

#include "internal/Buttons.h"
#include "internal/Switches.h"
#include "internal/SyncingSwitches.h"
#include "internal/Encoders.h"
#include "internal/Potentiometers.h"
#include "internal/RotarySyncingPotentiometer.h"
#include "internal/Leds.h"
#ifndef DCSBIOS_DISABLE_SERVO
#include "internal/Servos.h"
#endif
#include "internal/Dimmer.h"
#include "internal/BcdWheels.h"
#include "internal/AnalogMultiPos.h"
#include "internal/RotarySwitch.h"
#if defined(USE_MATRIX_SWITCHES) || defined(DCSBIOS_USE_MATRIX_SWITCHES)
#include "internal/MatrixSwitches.h"
#endif
#include "internal/DualModeButton.h"

namespace DcsBios {
	template<unsigned int first, unsigned int second>
	unsigned int piecewiseMap(unsigned int newValue) {
		return 0;
	}

	template<unsigned int from1, unsigned int to1, unsigned int from2, unsigned int to2, unsigned int... rest>
	unsigned int piecewiseMap(unsigned int newValue) {
		if (newValue < from2) {
			return map(newValue, from1, from2, to1, to2);
		} else {
			return piecewiseMap<from2, to2, rest...>(newValue);
		}
	}
}

#ifndef DCSBIOS_RS485_MASTER
namespace DcsBios {	
	inline bool sendDcsBiosMessage(const char* msg, const char* arg) {
		while(!tryToSendDcsBiosMessage(msg, arg));
		return true;
	}
}

// for backwards compatibility, can be removed when we have a proper place to document this interface:
inline bool sendDcsBiosMessage(const char* msg, const char* arg) {
	while(!DcsBios::tryToSendDcsBiosMessage(msg, arg));
	return true;
}
#endif

#endif // include guard
