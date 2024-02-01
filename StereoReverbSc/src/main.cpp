/**
 * @file main.cpp
 * @author Piotr Zapart
 * @brief Example project for the Stereo Reverb SC component
 * 		required libraries: 
 * 			OpenAudio_ArduinoLibrary: https://github.com/chipaudette/OpenAudio_ArduinoLibrary
 * 			HexeFX_audiolib_F32: https://github.com/hexeguitar/hexefx_audiolib_F32
 * 			BasicTerm:	https://github.com/nottwo/BasicTerm
 * 			
 * @version 0.1
 * @date 2024-01-11
 * 
 * @copyright Copyright www.hexefx.com (c) 2024
 * 
 */
#include <Arduino.h>
#include "Audio.h"
#include "OpenAudio_ArduinoLibrary.h"
#include "hexefx_audio_F32.h"
#include "BasicTerm.h"
#include "stats.h"

// uncomment the line below to make examlpe work with TeensyAudioAdapter board (SGTL5000)
//#define USE_TEENSY_AUDIO_BOARD

#ifndef DBG_SERIAL 
	#define DBG_SERIAL Serial
#endif

// analog bypass controls
#define DRY_CTRL_PIN    28
#define WET_CTRL_PIN    29
#define CTRL_HI     	LOW
#define CTRL_LO     	HIGH

// Teensy audio adaptor is using I2S1 and SGTL5000 codec chip
#ifdef USE_TEENSY_AUDIO_BOARD
AudioControlSGTL5000			codec;
AudioInputI2S_F32				i2s_in;
AudioEffectPlateReverb_F32		reverb;
AudioOutputI2S_F32     			i2s_out; 
#else
// HW configuration for the HexeFX T41.GFX pedal (I2S2 + WM8731 coded)
AudioControlWM8731              codec;
AudioInputI2S2_F32				i2s_in;
AudioEffectReverbSc_F32			reverb;	// use DMARAM (default) for the main delay buffer or
//AudioEffectReverbSc_F32 reverb = AudioEffectReverbSc_F32(true);	// use external PSRAM on Teensy4.1 
AudioOutputI2S2_F32     		i2s_out;
#endif
 
// reverb has it's own internal dry/wet mixer
AudioConnection_F32     cable1(i2s_in, 0, reverb, 0);
AudioConnection_F32     cable2(i2s_in, 1, reverb, 1);
AudioConnection_F32     cable3(reverb, 0, i2s_out, 0);
AudioConnection_F32     cable4(reverb, 1, i2s_out, 1);

BasicTerm term(&DBG_SERIAL); // terminal is used to print out the status and info via WebSerial

// Callbacks for MIDI
void cb_NoteOn(byte channel, byte note, byte velocity);
void cb_ControlChange(byte channel, byte control, byte value);

int8_t pitch=0, pitchShm=0;
uint32_t timeNow, timeLast;

void printMemInfo(void);

void setup()
{
	DBG_SERIAL.begin(115200);
	DBG_SERIAL.println("T41GFX - Stereo Plate Reverb");
	DBG_SERIAL.println("01.2024 www.hexefx.com");
	// analog IO setup - depends on used hardware
	pinMode(DRY_CTRL_PIN, OUTPUT);
	pinMode(WET_CTRL_PIN, OUTPUT);
	digitalWriteFast(DRY_CTRL_PIN, CTRL_LO); 	// mute analog dry passthrough
	digitalWriteFast(WET_CTRL_PIN, CTRL_HI);	// turn on wet signal
	
	AudioMemory_F32(20);

#ifdef USE_TEENSY_AUDIO_BOARD
	if (!codec.enable()) DBG_SERIAL.println("Codec init error!");
	codec.inputSelect(AUDIO_INPUT_LINEIN);
	codec.volume(0.8f);
	codec.lineInLevel(10, 10);
	codec.adcHighPassFilterDisable();
#else
    if (!codec.enable()) DBG_SERIAL.println("Codec init error!");
    codec.inputSelect(AUDIO_INPUT_LINEIN);
    codec.inputLevel(0.77f);
#endif
	DBG_SERIAL.println("Codec initialized.");
	// set callbacks for USB MIDI
    usbMIDI.setHandleNoteOn(cb_NoteOn);
    usbMIDI.setHandleControlChange(cb_ControlChange);

	//reverb.bypass_set(false);
	term.init();
    term.cls();
    term.show_cursor(false);
}

void loop()
{
	usbMIDI.read();
	timeNow = millis();
    if (timeNow - timeLast > 500)
    {
        term.position(1,0);
        printMemInfo();		
        timeLast = timeNow;
	}
}

/**
 * @brief USB MIDI NoteOn callback
 * 
 * @param channel 
 * @param note 
 * @param velocity 
 */
void cb_NoteOn(byte channel, byte note, byte velocity)
{
    switch(note)
    {
        case 1:
			reverb.bypass_tgl();
            break;
        case 2:
			reverb.freeze_tgl();
            break;
        case 3:
			digitalToggleFast(DRY_CTRL_PIN);
            break;
        case 4:
			digitalToggleFast(WET_CTRL_PIN);
            break;
        case 17:
            SCB_AIRCR = 0x05FA0004; // MCU reset
            break;
        default:
            break;
    }
}

void cb_ControlChange(byte channel, byte control, byte value)
{
    float32_t tmp = (float32_t) value / 127.0f;
    switch(control)
    {
        case 80:
            reverb.feedback(tmp);
            break;
        case 81:
			//tmp = map(tmp, 0.0f, 1.0f, 1000.0f, 16000.0f); 
			reverb.lowpass(tmp);  		
            break;
        case 82:
			reverb.mix(tmp);
            break;
        case 83:

            break;
        case 84:
            break;
        case 85:
            break;
        case 86:
            break;
        case 87:
            break;
        case 88:
            break;
        case 89:
            break;
        case 90:
            break;
        case 91:		
            break;
        default:    break;
    }
}

void printMemInfo(void)
{
    const char msg_on[] = "\x1b[32mon \x1b[0m";
    const char msg_off[] = "\x1b[31moff\x1b[0m";
	const char msg_ram2[] = "RAM2";
	const char msg_extram[] = "EXT PSRAM";
	const char msg_ram1[] = "RAM1";
	const char msg_none[] = "???";
	const char *ramtype = msg_none;
    float load_rv = reverb.processorUsageMax();
    reverb.processorUsageMaxReset();
	uint32_t dlyBfAddr  = reverb.getBfAddr();
	// check the location of the reverb delay buffer
	switch(dlyBfAddr)
	{
		case 0x20000000 ... 0x2007FFFF:	// buffer placed in RAM1
			ramtype = msg_ram1;
			break;
		case 0x20200000 ... 0x2027FFFF:	// buffer placed in RAM2
			ramtype = msg_ram2;
			break;
		case 0x70000000 ... 0x7EFFFFFF:	// buffer placed in external PSRAM
			ramtype = msg_extram;
			break;
		default:
			ramtype = msg_none;
			break;
	}

    float load = AudioProcessorUsageMax();
    AudioProcessorUsageMaxReset();

    DBG_SERIAL.printf("CPU usage: reverb = %2.2f%% max = %2.2f%%   \r\n", load_rv, load);
	DBG_SERIAL.printf("Reverb %s \tFreeze %s\r\n", 
					reverb.bypass_get() ? msg_off : msg_on,
					reverb.freeze_get() ? msg_on : msg_off);
	DBG_SERIAL.printf("Reverb buffer address = 0x%08X = %s\r\n", dlyBfAddr, ramtype);
}