#ifndef _FILTER_IR_CABSIM_H
#define _FILTER_IR_CABSIM_H

#include <Arduino.h>
#include <Audio.h>
#include "AudioStream.h"
#include "AudioStream_F32.h"
#include "arm_math.h"
#include "filter_ir_cabsim_irs.h"

#define IR_BUFFER_SIZE  128
#define IR_NFORMAX      (8192 / IR_BUFFER_SIZE)
#define IR_FFT_LENGTH   (2 * IR_BUFFER_SIZE)
#define IR_N_B          (1)
#define IR_MAX_REG_NUM  11       // max number of registered IRs

class AudioFilterIRCabsim_F32 : public AudioStream_F32
{
public:
    AudioFilterIRCabsim_F32();
    virtual void update(void);
    void ir_register(const float32_t *irPtr, uint8_t position);
    void ir_load(uint8_t idx);
    uint8_t ir_get(void) {return ir_idx;} 
    float ir_get_len_ms(void)
    {
        float32_t slen = 0.0f;
        if (irPtrTable[ir_idx])      slen = irPtrTable[ir_idx][0];
        return (slen / AUDIO_SAMPLE_RATE_EXACT)*1000.0f;
    }
private:
    audio_block_f32_t *inputQueueArray_f32[2];
    float32_t audio_gain = 0.3f;
    int idx_t = 0;
    int16_t *sp_L;
    int16_t *sp_R;
    const uint32_t FFT_L = IR_FFT_LENGTH;
    uint8_t first_block = 1;
    uint8_t ir_loaded = 0;  
    uint8_t ir_idx = 0;
    uint32_t nfor = 0;
    const int nforMax = IR_NFORMAX;

    int buffidx = 0;
    int k = 0;

	float32_t *ptr_fftout;
	float32_t *ptr_fmask;
	float32_t* ptr1;
	float32_t* ptr2;
	int k512;
	int j512;
	
    uint32_t N_BLOCKS = IR_N_B;

	// default IR table, use NULL for bypass
    const float32_t *irPtrTable[IR_MAX_REG_NUM] = 
    {
        ir_1_guitar, ir_2_guitar, ir_3_guitar, ir_4_guitar, ir_10_guitar, ir_11_guitar, ir_6_guitar, ir_7_bass,  ir_8_bass, ir_9_bass, NULL
    };
    void init_partitioned_filter_masks(const float32_t *irPtr);

}; 


#endif // _FILTER_IR_CONVOLVER_H
