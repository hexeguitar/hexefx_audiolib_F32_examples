/**
 * @file StereoIRcabsim_ArduinoIDE.ino
 * @author Piotr Zapart
 * @brief Example project for the Stereo IR cabsim component
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

#define DBG_SERIAL Serial

AudioControlSGTL5000			codec;
AudioInputI2S_F32				i2s_in;
AudioFilterToneStackStereo_F32	eq;
AudioFilterEqualizer_HX_F32		eqPreL;
AudioFilterEqualizer_HX_F32		eqPreR;
AudioFilterIRCabsim_F32			cabsim;
AudioFilterEqualizer_HX_F32		eqL;
AudioFilterEqualizer_HX_F32		eqR;
AudioOutputI2S_F32     			i2s_out;


AudioConnection_F32     cable1(i2s_in, 0, eq, 0);
AudioConnection_F32     cable2(i2s_in, 1, eq, 1);

AudioConnection_F32		cable3(eq, 0, eqPreL, 0);
AudioConnection_F32		cable4(eq, 1, eqPreR, 0);

AudioConnection_F32 	cable10(eqPreL, 0, cabsim, 0);
AudioConnection_F32 	cable11(eqPreR, 0, cabsim, 1);
AudioConnection_F32     cable30(cabsim, 0, eqL, 0);	// post EQ
AudioConnection_F32     cable31(cabsim, 1, eqR, 0);

AudioConnection_F32		cable40(eqL, 0, i2s_out, 0);
AudioConnection_F32		cable41(eqR, 0, i2s_out, 1);

/**
 * @brief eqR is a 10 band equalizer used on channelR if Doubler (stereo) mode is enabled in the IR cabsim
 * 			it helps to restore the LR balance if one channel is delayed. Also decorelates channel R which 
 * 			helps with createing a wider and more natual stereo signal.
 */
const float32_t fBandPre[] = {	40.0f, 	80.0f, 	160.0f, 	320.0f, 	640.0f, 	1280.0f, 	2560.0f, 	5120.0f, 	10240.0f, 	22050.0f};
const float32_t dbBandPreR[] = {	0.0f,  	2.0f, 	-2.0f,  	-3.0f, 		-1.0f,  	2.0f,   	3.0f,   	4.0f,    	0.0f,    	-100.0f};
const float32_t dbBandPreL[] = {	0.0f,  	-2.0f, 	1.0f,  		 0.0f, 		-2.0f,  	-1.0f,   	-1.4f,   	0.0f,    	0.0f,    	-100.0f};
float32_t equalizeCoeffs[200];
const float32_t fBandPost[] = {	40.0f, 	100.0f, 	250.0f, 	5000.0f,  	7600.0f, 	22050.0f};
const float32_t dbBandPostR[]= {	0.0f, 	3.6f, 		0.0f, 		0.0f, 		2.5f, 		-100.0f}; 
const float32_t dbBandPostL[]= {	0.0f, 	1.6f, 		0.0f, 		0.0f, 		-1.5f, 		-100.0f}; 

BasicTerm term(&DBG_SERIAL); // terminal is used to print out the status and info via WebSerial

// Callbacks for MIDI
void cb_NoteOn(byte channel, byte note, byte velocity);
void cb_ControlChange(byte channel, byte control, byte value);

bool doublerState = true;
uint8_t IRno = 6;
bool eq_bypass = false;
const char *eqPresetName;
uint32_t timeNow, timeLast;
const char msg_OFF[] = "OFF";

void printMemInfo(void);

void setup()
{
	DBG_SERIAL.begin(115200);
	DBG_SERIAL.println("T41GFX - Stereo IR Cabsim");
	DBG_SERIAL.println("01.2024 www.hexefx.com");

	AudioMemory_F32(20);
	eqPreL.equalizerNew(10, (float *)&fBandPre[0], (float *)&dbBandPreL[0], 30, &equalizeCoeffs[0], 60.0f);
	eqPreR.equalizerNew(10, (float *)&fBandPre[0], (float *)&dbBandPreR[0], 30, &equalizeCoeffs[0], 60.0f);
	eqL.equalizerNew(6, (float *)&fBandPost[0], (float *)&dbBandPostL[0], 30, &equalizeCoeffs[0], 60.0f);
	eqR.equalizerNew(6, (float *)&fBandPost[0], (float *)&dbBandPostR[0], 30, &equalizeCoeffs[0], 60.0f);

	if (!codec.enable()) DBG_SERIAL.println("Codec init error!");
	codec.inputSelect(AUDIO_INPUT_LINEIN);
	codec.volume(0.8f);
	codec.lineInLevel(10, 10);
	codec.adcHighPassFilterDisable();

	DBG_SERIAL.println("Codec initialized.");
	// set callbacks for USB MIDI
    usbMIDI.setHandleNoteOn(cb_NoteOn);
    usbMIDI.setHandleControlChange(cb_ControlChange);
	// default sound settings:
	cabsim.ir_load(IRno);
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
            break;
        case 2:
            break;
		case 6 ... 16:
			IRno = note - 6;
			cabsim.ir_load(IRno);
			break;
        case 17:
            SCB_AIRCR = 0x05FA0004; // MCU reset
            break;
		case 18 ... 27:
			eq.setModel((toneStack_presets_e)(note-18));
			eqPresetName = eq.getName();
			break;
		case 28:
			eq.setModel((toneStack_presets_e)(note-18));
			eqPresetName = msg_OFF;
			break;
		case 30:
			doublerState = cabsim.doubler_tgl();
			eq_bypass = !doublerState;
			eqPreR.bypass_set(eq_bypass);
			eqPreL.bypass_set(eq_bypass);
			eqL.bypass_set(eq_bypass);
			eqR.bypass_set(eq_bypass);
			break;
		case 31:	
			eq_bypass = eqPreR.bypass_tgl();
			eqPreL.bypass_set(eq_bypass);
			eqL.bypass_set(eq_bypass);
			eqR.bypass_set(eq_bypass);			
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
    DBG_SERIAL.printf("CPU usage: cabsim=%2.2f%% eq=%2.2F%%  max = %2.2f%%   \r\n",
						 load_rv, load_eq, load);
	DBG_SERIAL.printf("EQ model: %s      \r\n", eqPresetName);
	DBG_SERIAL.printf("Doubler %s\r\n", doublerState ? on : off);	
	DBG_SERIAL.print("IR: ");
	DBG_SERIAL.print(bf);
}
