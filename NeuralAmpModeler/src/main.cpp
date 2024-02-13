/**
 * @file main.cpp
 * @author Piotr Zapart
 * @brief Neural network guitar amp emulation project,
 * 		--------------------------------------------------------
 * 		----- REQUIRES TEENSY4.1 with PSRAM installed ! --------		
 * 		--------------------------------------------------------
 * 		Signal chain:
 * 		input ------>amp ------> tone stack -> gate -> delay -> reverb -> Cabsim IR + Doubler
 * 			|-----> noise gate side chain ------^ 
 * 					
 * 		required libraries: 
 * 			OpenAudio_ArduinoLibrary: https://github.com/chipaudette/OpenAudio_ArduinoLibrary
 * 			HexeFX_audiolib_F32: https://github.com/hexeguitar/hexefx_audiolib_F32
 * 			BasicTerm:	https://github.com/nottwo/BasicTerm
 * 			
 * 		Based on Seed pedal design by
 * 			Keith Bloemer (GuitarML) https://github.com/GuitarML/Seed
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
#include "RTNeural_F32.h"

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
AudioEffectNoiseGateStereo_F32	gate;
AudioEffectRTNeural_F32			amp;
AudioFilterEqualizer3bandStereo_F32	toneStack;
AudioEffectDelayStereo_F32		echo=AudioEffectDelayStereo_F32(1000, true); // 1 sec delay, buffer in PSRAM;
AudioEffectSpringReverb_F32		reverb;
AudioEffectGainStereo_F32		masterVol;
AudioFilterIRCabsim_F32			cabsim;


AudioOutputI2S_F32     			i2s_out;
#else
// HW configuration for the HexeFX T41.GFX pedal (I2S2 + WM8731 coded)
AudioControlWM8731              codec;
AudioInputI2S2_F32				i2s_in;
AudioEffectNoiseGateStereo_F32	gate;
AudioEffectRTNeural_F32			amp;
AudioFilterEqualizer3bandStereo_F32	toneStack;
AudioEffectDelayStereo_F32		echo=AudioEffectDelayStereo_F32(1000, true); // 1 sec delay, buffer in PSRAM;
AudioEffectSpringReverb_F32		reverb;
AudioEffectGainStereo_F32		masterVol;
AudioFilterIRCabsim_F32			cabsim;
AudioOutputI2S2_F32     		i2s_out;
#endif

AudioConnection_F32     cable0(i2s_in, 0, gate, 2); // gate side chaain input
AudioConnection_F32     cable1(i2s_in, 1, gate, 3);
AudioConnection_F32     cable2(i2s_in, 0, amp, 0);	// amp input
AudioConnection_F32     cable3(i2s_in, 1, amp, 1);
AudioConnection_F32		cable4(amp, 0, toneStack, 0); 
AudioConnection_F32		cable5(amp, 1, toneStack, 1);
AudioConnection_F32 	cable6(toneStack, 0, gate, 0);
AudioConnection_F32 	cable7(toneStack, 1, gate, 1); 
AudioConnection_F32		cable8(gate, 0, echo, 0);	// gate gain element into delay
AudioConnection_F32		cable9(gate, 1, echo, 1);
AudioConnection_F32		cable10(echo, 0, reverb, 0);	// delay -> reverb
AudioConnection_F32		cable11(echo, 1, reverb, 1);
AudioConnection_F32		cable12(reverb, 0, masterVol, 0); // reverb-> master volume
AudioConnection_F32		cable13(reverb, 1, masterVol, 1);
// --- stereo IR canbsim + doubler ---
AudioConnection_F32		cable20(masterVol, 0, cabsim, 0);
AudioConnection_F32		cable21(masterVol, 1, cabsim, 1);
AudioConnection_F32     cable50(cabsim, 0, i2s_out, 0);
AudioConnection_F32     cable51(cabsim, 1, i2s_out, 1);

BasicTerm term(&DBG_SERIAL); // terminal is used to print out the status and info via WebSerial

// Callbacks for MIDI
void cb_NoteOn(byte channel, byte note, byte velocity);
void cb_ControlChange(byte channel, byte control, byte value);
void cb_MidiClock(void);

bool MIDIcomm_indicator = false;
bool doublerState = false;
bool reverbState = false;
bool delayState = false;
uint8_t IRno = 6;
uint32_t timeNow, timeLast;

void printMemInfo(void);

void setup()
{
	DBG_SERIAL.begin(115200);
	DBG_SERIAL.println("T41GFX - Guitar Amp Emulation");
	DBG_SERIAL.println("02.2024 www.hexefx.com");
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
	usbMIDI.setHandleClock(cb_MidiClock);

	amp.changeModel(0);
	// default sound settings:
	cabsim.ir_load(IRno);
	cabsim.doubler_set(false);
	doublerState = cabsim.doubler_get();

	masterVol.setGain(1.0f);

	gate.setOpeningTime(0.01f);
	gate.setClosingTime(0.05f);
	gate.setHoldTime(0.2f);
	gate.setThreshold(-65);

	reverb.time(0.6f);
	reverb.bass_cut(0.75f);
	reverb.treble_cut(0.8f);
	reverb.mix(0.2f);

	echo.bypass_set(true); // delay off at start
	echo.time(0.5f);
	echo.feedback(0.3f);
	echo.mix(0.20f);
	reverb.bypass_set(true);

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
	MIDIcomm_indicator = true;
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
		case 18:
			delayState = !echo.bypass_tgl();
			break;
		case 19:
			echo.tap_tempo(true); // with averaging
		case 28:
			break;
		case 30:
			doublerState = cabsim.doubler_tgl();
			break;
		case 31:
			reverb.bypass_set(true); // reverb OFF
			reverbState = false;		
			break;
		case 32:
			reverb.bypass_set(false); // reverb OFF
			reverbState = true;			
			break;
		case 33: 
			echo.bypass_set(true);
			delayState = false;
			break;
		case 34:
			echo.bypass_set(false);
			delayState = true;
			break;
		case 35:
			cabsim.doubler_set(false);
			doublerState = false;
			break;
		case 36:
			cabsim.doubler_set(true);
			doublerState = true;
			break;
		case 37:
			reverbState = !reverb.bypass_tgl();
			break;
		case 40 ... 48:
			amp.changeModel(note-40);
			break;
        default:
            break;
    }
}

void cb_ControlChange(byte channel, byte control, byte value)
{
    float32_t tmp = (float32_t) value / 127.0f;
	MIDIcomm_indicator = true;
    switch(control)
    {
		case 1:
            echo.time(tmp);
            break;
        case 2:
			echo.inertia(tmp);
            break;
        case 3: 
			echo.feedback(tmp);
            break;
        case 4:
			echo.mix(tmp);
            break;
        case 5:
			echo.mod_rate(tmp);
            break;
        case 6:
			echo.mod_depth(tmp);
            break;
        case 7:
			echo.treble_cut(tmp);
            break;
        case 8:
			echo.bass_cut(tmp);
            break;
        case 9:
			echo.treble(tmp);
            break;
        case 10:
			echo.bass(tmp);
            break;
        case 80:
			gate.setThreshold(tmp * -100.0f);
            break;
        case 81:
			toneStack.bass(tmp);
            break;
        case 82:
			toneStack.mid(tmp);
            break;
        case 83:
			toneStack.treble(tmp);
            break;
        case 84:
			masterVol.setGain(tmp);
            break;
        case 85:
			amp.gain(tmp);
            break;	
        case 86:
			reverb.mix(tmp);
            break;		
        case 87:
			reverb.time(tmp);
            break;	
        case 88:
			reverb.bass_cut(tmp);
            break;	
        case 89:
			reverb.treble_cut(tmp);
            break;													
        default:    break;
    }
}

void cb_MidiClock(void)
{
	static uint32_t clk_count = 0;
	MIDIcomm_indicator = true;
	if (++clk_count >= 24)
	{
		echo.tap_tempo(false);
		// do someting, ie: Delay tap tempo
		clk_count = 0;
	}
}

void printMemInfo(void)
{
    const char *on = "\x1b[32mon \x1b[0m";
    const char *off = "\x1b[31moff\x1b[0m";

	float32_t load_amp = amp.processorUsageMax();
	amp.processorUsageMaxReset();
    float32_t load_cb = cabsim.processorUsageMax();
    cabsim.processorUsageMaxReset();
	float32_t load_eq = toneStack.processorUsageMax();
	toneStack.processorUsageMaxReset();
	float32_t load_gate = gate.processorUsageMax();
	gate.processorUsageMaxReset();
	float32_t load_rv = reverb.processorUsageMax();
	reverb.processorUsageMaxReset();
	float32_t load_dly = echo.processorUsageMax();
	echo.processorUsageMaxReset();

	float32_t load = AudioProcessorUsageMax();
    AudioProcessorUsageMaxReset();
	char bf[20] = "";
	
	switch(IRno)
	{
		case 0 ... 6:
			snprintf(bf, 20, "Guitar %d    \r\n", IRno+1);
			break;
		case 7 ... 9:
			snprintf(bf, 20, "Bass %d    \r\n", IRno-6);
			break;
		case 10:
			snprintf(bf, 20, "OFF        \r\n");
			break;
		default: break;
	}
    DBG_SERIAL.printf("CPU usage: amp=%2.2f%% cabsim=%2.2f%% tone=%2.2F%%    \r\n",
						 load_amp, load_cb, load_eq);
    DBG_SERIAL.printf("           gate=%2.2f%% delay=%2.2f%% reverb=%2.2f%% max = %2.2f%%     \r\n",
						 load_gate, load_dly, load_rv, load);						 
	DBG_SERIAL.printf("Doubler %s Reverb %s Delay %s  \r\n", 
							doublerState ? on : off,
							reverbState ? on : off,
							delayState ? on : off);
	uint8_t model = amp.getModel();
	switch(model)
	{
		case 0:
			snprintf(bf, 20, "OFF        \r\n"); 
			break;
		case 1 ... 4:
			snprintf(bf, 20, "Clean %d    \r\n", model);
			break;
		case 5 ... 6:
			snprintf(bf, 20, "Crunch %d    \r\n", model-4);
			break;
		case 7 ... 8:
			snprintf(bf, 20, "Lead %d    \r\n", model-6);
			break;
		default: break;
	}							
	DBG_SERIAL.print("Amp model: ");
	DBG_SERIAL.print(bf);
	switch(IRno)
	{
		case 0 ... 6:
			snprintf(bf, 20, "Guitar %d    \r\n", IRno+1);
			break;
		case 7 ... 9:
			snprintf(bf, 20, "Bass %d    \r\n", IRno-6);
			break;
		case 10:
			snprintf(bf, 20, "OFF        \r\n");
			break;
		default: break;
	}
	DBG_SERIAL.print("IR: ");
	DBG_SERIAL.print(bf);
}

