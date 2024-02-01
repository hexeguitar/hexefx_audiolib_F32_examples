/**
 * @file main.cpp
 * @author Piotr Zapart
 * @brief Example project for the Stereo Plate Reverb component
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
AudioFilterToneStackStereo_F32	eq;
AudioFilterIRCabsim_F32			cabsim;
AudioOutputI2S_F32     			i2s_out;
#else
// HW configuration for the HexeFX T41.GFX pedal (I2S2 + WM8731 coded)
AudioControlWM8731              codec;
AudioInputI2S2_F32				i2s_in;
AudioFilterToneStackStereo_F32	eq;
AudioFilterIRCabsim_F32			cabsim;
AudioOutputI2S2_F32     		i2s_out;
#endif

AudioConnection_F32     cable1(i2s_in, 0, eq, 0);
AudioConnection_F32     cable2(i2s_in, 1, eq, 1);
AudioConnection_F32		cable10(eq, 0, cabsim, 0);
AudioConnection_F32		cable11(eq, 1, cabsim, 1);
AudioConnection_F32		cable21(cabsim, 0, i2s_out, 0);
AudioConnection_F32		cable22(cabsim, 1, i2s_out, 1);

BasicTerm term(&DBG_SERIAL); // terminal is used to print out the status and info via WebSerial

// Callbacks for MIDI
void cb_NoteOn(byte channel, byte note, byte velocity);
void cb_ControlChange(byte channel, byte control, byte value);

bool doublerState = false;
uint8_t IRno = 6;
uint8_t eqModelNo = 0;
const char *eqPresetName;
uint32_t timeNow, timeLast;
const char msg_OFF[] = "OFF";

void printMemInfo(void);

void setup()
{
	DBG_SERIAL.begin(115200);
	DBG_SERIAL.println("T41GFX - Stereo Plate Reverb");
	DBG_SERIAL.println("01.2024 www.hexefx.com");
#ifndef USE_TEENSY_AUDIO_BOARD	
	// analog IO setup - depends on used hardware
	pinMode(DRY_CTRL_PIN, OUTPUT);
	pinMode(WET_CTRL_PIN, OUTPUT);
	digitalWriteFast(DRY_CTRL_PIN, CTRL_LO); 	// mute analog dry passthrough
	digitalWriteFast(WET_CTRL_PIN, CTRL_HI);	// turn on wet signal
#endif	
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
	// default sound settings:
	cabsim.ir_load(IRno);
	eqModelNo = TONESTACK_MESA;
	eq.setModel(TONESTACK_MESA);
	eq.setTone(0.2f, 0.7f, 0.75f);
	eq.setGain(0.8f * 4.0f);
	eqPresetName = eq.getName();

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
			digitalToggleFast(DRY_CTRL_PIN);
            break;
        case 2:
			digitalToggleFast(WET_CTRL_PIN);
            break;
		case 6 ... 16:
			IRno = note - 6;
			cabsim.ir_load(IRno);
			break;
        case 17:
            SCB_AIRCR = 0x05FA0004; // MCU reset
            break;
		case 18 ... 27:
			eqModelNo = note - 18;
			eq.setModel((toneStack_presets_e)(eqModelNo));
			eqPresetName = eq.getName();
			break;
		case 28:
			break;
		case 30:
			doublerState = cabsim.doubler_tgl();
			break;
		case 31:	
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
            break;
        case 81:
			eq.setBass(tmp);
            break;
        case 82:
			eq.setMid(tmp);
            break;
        case 83:
			eq.setTreble(tmp);
            break;
        case 84:
			eq.setGain(tmp*4.0f);
            break;
        default:    break;
    }
}

void printMemInfo(void)
{
    const char *on = "\x1b[32mon \x1b[0m";
    const char *off = "\x1b[31moff\x1b[0m";
    float load_rv = cabsim.processorUsageMax();
    cabsim.processorUsageMaxReset();
	float load_eq = eq.processorUsageMax();
	eq.processorUsageMaxReset();
	float load = AudioProcessorUsageMax();
    AudioProcessorUsageMaxReset();
	char bf[20] = "";
	
	float32_t irlen = cabsim.ir_get_len_ms();

	switch(IRno)
	{
		case 0 ... 6:
			snprintf(bf, 40, "Guitar %d %2.2fms    \r\n", IRno+1, irlen);
			break;
		case 7 ... 9:
			snprintf(bf, 40, "Bass %d %2.2fms   \r\n", IRno-6, irlen);
			break;
		case 10:
			snprintf(bf, 40, "OFF               \r\n");
			break;
		default: break;
	}
    DBG_SERIAL.printf("CPU usage: cabsim=%2.2f%% eq=%2.2F%%  max = %2.2f%%   \r\n",
						 load_rv, load_eq, load);
	DBG_SERIAL.printf("EQ model: %d %s      \r\n",eqModelNo, eqPresetName);
	DBG_SERIAL.printf("Doubler %s\r\n", doublerState ? on : off);	
	DBG_SERIAL.print("IR: ");
	DBG_SERIAL.print(bf);
}
