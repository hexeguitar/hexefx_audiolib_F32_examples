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
AudioEffectPlateReverb_F32		reverb;
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

	reverb.bypass_set(false);
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
            reverb.size(tmp);
            break;
        case 81:
			reverb.diffusion(tmp);
            break;
        case 82:
			reverb.mix(tmp, 1.0f-tmp);
            break;
        case 83:
			reverb.lodamp(tmp);  
            break;
        case 84:
			reverb.hidamp(tmp);
            break;
        case 85:
		    reverb.lowpass(tmp);                
            break;
        case 86:
            reverb.hipass(tmp);
            break;
        case 87:
			reverb.freezeBleedIn(tmp);
            break;
        case 88:
			value = map(value, 0, 127, -12, 24);
			pitch = value;
			reverb.pitchSemitones(value);
            break;
        case 89:
			reverb.pitchMix(tmp);
            break;
        case 90:
			reverb.shimmer(tmp);
            break;
        case 91:
			value = map(value, 0, 127, -12, 24);
			pitchShm = value;
			reverb.shimmerPitchSemitones(value);		
            break;
        default:    break;
    }

}

void printMemInfo(void)
{
    const char *on = "\x1b[32mon \x1b[0m";
    const char *off = "\x1b[31moff\x1b[0m";
    float load_rv = reverb.processorUsageMax();
    reverb.processorUsageMaxReset();

    float load = AudioProcessorUsageMax();
    AudioProcessorUsageMaxReset();

    DBG_SERIAL.printf("CPU usage: reverb = %2.2f%% max = %2.2f%%   \r\n", load_rv, load);

    DBG_SERIAL.printf("Reverb %s \tFreeze %s\r\n", 
                        reverb.bypass_get()?off:on,
                        reverb.freeze_get()?on:off);
    DBG_SERIAL.printf("Reverb pitch \t%d semitones          \r\n", pitch);
	DBG_SERIAL.printf("Shimmer pitch \t%d semitones          \r\n", pitchShm);			

}