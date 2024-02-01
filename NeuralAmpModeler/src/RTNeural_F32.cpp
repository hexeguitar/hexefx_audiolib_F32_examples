/**
 * @file RTNeural_F32.cpp
 * @author Piotr Zapart www.hexefx.com
 * @brief Based on Seed pedal design by
 * 		 Keith Bloemer (GuitarML) https://github.com/GuitarML/Seed
 * 		Ported to Teensy4.1 with the following additions/changes:
 * 			- adjusted model volume to match unity gain with bypass signal
 * 			- dual mono input and output
 * 			- added bypass system
 * @version 0.1
 * @date 2024-01-31
 */
#include "RTNeural_F32.h"
#include "RTNeural_models.h"

AudioEffectRTNeural_F32::AudioEffectRTNeural_F32() : AudioStream_F32(2, inputQueueArray_f32)
{
	setupWeights();
	initialized =true;
}

void AudioEffectRTNeural_F32::changeModel(uint8_t modelNo)
{
	if (modelNo == 0)
	{
		bp = true;
		return;
	}
    if ( modelNo > (model_collection.size()) ) return; 
	else 
	{
		bp = false;
        modelIndex = modelNo - 1;
        auto& gru = (model).template get<0>();
        auto& dense = (model).template get<1>();
		__disable_irq();
        gru.setWVals(model_collection[modelIndex].rec_weight_ih_l0);
        gru.setUVals(model_collection[modelIndex].rec_weight_hh_l0);
        gru.setBVals(model_collection[modelIndex].rec_bias);
        dense.setWeights(model_collection[modelIndex].lin_weight);
        dense.setBias(model_collection[modelIndex].lin_bias.data());
        model.reset();
        nnLevelAdjust = model_collection[modelIndex].levelAdjust;
		__enable_irq();
    }
}

void AudioEffectRTNeural_F32::update()
{
	if (!initialized) return;
	audio_block_f32_t *blockL, *blockR;
	int16_t i;
	float32_t sigIn[1] = {0.0f};
	float32_t output;

	if (bp) // handle bypass
	{
		blockL = AudioStream_F32::receiveReadOnly_f32(0);
		blockR = AudioStream_F32::receiveReadOnly_f32(1);
		if (!blockL || !blockR) 
		{
			if (blockL) AudioStream_F32::release(blockL);
			if (blockR) AudioStream_F32::release(blockR);
			return;
		}
		AudioStream_F32::transmit(blockL, 0);	
		AudioStream_F32::transmit(blockR, 1);
		AudioStream_F32::release(blockL);
		AudioStream_F32::release(blockR);
		return;
	}
	blockL = AudioStream_F32::receiveWritable_f32(0);
    blockR = AudioStream_F32::receiveWritable_f32(1);

	if (!blockL || !blockR) 
    {
		if (blockL) AudioStream_F32::release(blockL);
		if (blockR) AudioStream_F32::release(blockR);
		return;
	}
	for (i=0; i < blockL->length; i++) 
    {
		sigIn[0] = (blockL->data[i] + blockR->data[i]) * 0.5f * inputGain; // sum both channels
		output = model.forward(sigIn) + sigIn[0];
		output *= nnLevelAdjust;
		blockL->data[i] = output;
		blockR->data[i] = output;
	}
	AudioStream_F32::transmit(blockL, 0);
	AudioStream_F32::transmit(blockR, 1);
	AudioStream_F32::release(blockL);
	AudioStream_F32::release(blockR);
}