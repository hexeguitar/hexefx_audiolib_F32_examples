#include <Arduino.h>


void memInfo()
{
	constexpr auto RAM_BASE = 0x2020'0000;
	constexpr auto RAM_SIZE = 512 << 10;
	constexpr auto FLASH_BASE = 0x6000'0000;
#if ARDUINO_TEENSY40
	constexpr auto FLASH_SIZE = 2 << 20;
#elif ARDUINO_TEENSY41
	constexpr auto FLASH_SIZE = 8 << 20;
#endif

	// note: these values are defined by the linker, they are not valid memory
	// locations in all cases - by defining them as arrays, the C++ compiler
	// will use the address of these definitions - it's a big hack, but there's
	// really no clean way to get at linker-defined symbols from the .ld file

	extern char _stext[], _etext[], _sbss[], _ebss[], _sdata[], _edata[],
		_estack[], _heap_start[], _heap_end[], _itcm_block_count[], *__brkval;

	auto sp = (char *)__builtin_frame_address(0);

	Serial.printf("_stext        %08x\n", _stext);
	Serial.printf("_etext        %08x +%db\n", _etext, _etext - _stext);
	Serial.printf("_sdata        %08x\n", _sdata);
	Serial.printf("_edata        %08x +%db\n", _edata, _edata - _sdata);
	Serial.printf("_sbss         %08x\n", _sbss);
	Serial.printf("_ebss         %08x +%db\n", _ebss, _ebss - _sbss);
	Serial.printf("curr stack    %08x +%db\n", sp, sp - _ebss);
	Serial.printf("_estack       %08x +%db\n", _estack, _estack - sp);
	Serial.printf("_heap_start   %08x\n", _heap_start);
	Serial.printf("__brkval      %08x +%db\n", __brkval, __brkval - _heap_start);
	Serial.printf("_heap_end     %08x +%db\n", _heap_end, _heap_end - __brkval);
#if ARDUINO_TEENSY41
	extern char _extram_start[], _extram_end[], *__brkval;
	Serial.printf("_extram_start %08x\n", _extram_start);
	Serial.printf("_extram_end   %08x +%db\n", _extram_end,
		   _extram_end - _extram_start);
#endif
	Serial.printf("\n");

	Serial.printf("<ITCM>  %08x .. %08x\n",
		   _stext, _stext + ((int)_itcm_block_count << 15) - 1);
	Serial.printf("<DTCM>  %08x .. %08x\n",
		   _sdata, _estack - 1);
	Serial.printf("<RAM>   %08x .. %08x\n",
		   RAM_BASE, RAM_BASE + RAM_SIZE - 1);
	Serial.printf("<FLASH> %08x .. %08x\n",
		   FLASH_BASE, FLASH_BASE + FLASH_SIZE - 1);
#if ARDUINO_TEENSY41
	extern uint8_t external_psram_size;
	if (external_psram_size > 0)
		Serial.printf("<PSRAM> %08x .. %08x\n",
			   _extram_start, _extram_start + (external_psram_size << 20) - 1);
#endif
	Serial.printf("\n");

	auto stack = sp - _ebss;
	Serial.printf("avail STACK %8d b %5d kb\n", stack, stack >> 10);

	auto heap = _heap_end - __brkval;
	Serial.printf("avail HEAP  %8d b %5d kb\n", heap, heap >> 10);

#if ARDUINO_TEENSY41
	auto psram = _extram_start + (external_psram_size << 20) - _extram_end;
	Serial.printf("avail PSRAM %8d b %5d kb\n", psram, psram >> 10);
#endif
}

uint32_t *ptrFreeITCM;	 // Set to Usable ITCM free RAM
uint32_t sizeofFreeITCM; // sizeof free RAM in uint32_t units.
uint32_t SizeLeft_etext;

extern char _stext[], _etext[];
FLASHMEM void getFreeITCM()
{ // end of CODE ITCM, skip full 32 bits
	Serial.println("\n\n++++++++++++++++++++++");
	SizeLeft_etext = (32 * 1024) - (((uint32_t)&_etext - (uint32_t)&_stext) % (32 * 1024));
	sizeofFreeITCM = SizeLeft_etext - 4;
	sizeofFreeITCM /= sizeof(ptrFreeITCM[0]);
	ptrFreeITCM = (uint32_t *)((uint32_t)&_stext + (uint32_t)&_etext + 4);
	Serial.printf("Size of Free ITCM in Bytes = %u\n", sizeofFreeITCM * sizeof(ptrFreeITCM[0]));
	Serial.printf("Start of Free ITCM = %u [%X] \n", ptrFreeITCM, ptrFreeITCM);
	Serial.printf("End of Free ITCM = %u [%X] \n", ptrFreeITCM + sizeofFreeITCM, ptrFreeITCM + sizeofFreeITCM);
	for (uint32_t ii = 0; ii < sizeofFreeITCM; ii++)
		ptrFreeITCM[ii] = 1;
	uint32_t jj = 0;
	for (uint32_t ii = 0; ii < sizeofFreeITCM; ii++)
		jj += ptrFreeITCM[ii];
	Serial.printf("ITCM DWORD cnt = %u [#bytes=%u] \n", jj, jj * 4);
}