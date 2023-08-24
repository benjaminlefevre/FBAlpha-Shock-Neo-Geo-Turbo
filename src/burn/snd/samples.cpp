// FB Alpha sample player module

#include "burnint.h"
#include "samples.h"

#define SAMPLE_DIRECTORY	szAppSamplesPath

#define get_long()	((ptr[3] << 24) | (ptr[2] << 16) | (ptr[1] << 8) | (ptr[0] << 0))
#define get_short()	((ptr[1] << 8) | (ptr[0] << 0))

static inline UINT16 get_shorti(const UINT8* const p)
{
	return (p[1] << 8) | p[0];
}

static INT32 bAddToStream = 0;
static INT32 nTotalSamples = 0;
INT32 bBurnSampleTrimSampleEnd = 0;

struct sample_format
{
	UINT8 *data;
	UINT32 length;
	UINT64 position;
	UINT8 playing;
	UINT8 loop;
	UINT8 flags;
	INT32 playback_rate; // 100 = 100%, 200 = 200%, 
	double gain[2];
	INT32 output_dir[2];
};

static struct sample_format *samples		= NULL; // store samples
static struct sample_format *sample_ptr		= NULL; // generic pointer for sample

static void make_raw(UINT8 *src, UINT32 len)
{
	UINT8 *ptr = src;

	if (ptr[0] != 'R' || ptr[1] != 'I' || ptr[2] != 'F' || ptr[3] != 'F') return;
	                                    ptr += 4; // skip RIFF
	UINT32 length = get_long();		    ptr += 4; // total length of file
	if (len < length) length = len - 8;	    	  // first 8 bytes (RIFF + Len)

	/* "WAVEfmt " */			        ptr += 8; // WAVEfmt + 1 space
	UINT32 length2 = get_long();		ptr += 4; // Wavefmt length
/*	UINT16 format = get_short();  */    ptr += 2; // format?
	UINT16 channels = get_short();		ptr += 2; // channels
	UINT32 sample_rate = get_long();	ptr += 4; // sample rate
/*	UINT32 speed = get_long();      */  ptr += 4; // speed - should equal (bits * channels * sample_rate)
/*	UINT16 align = get_short();   */    ptr += 2; // block align	should be ((bits / 8) * channels)
	UINT16 bits = get_short() / 8;		ptr += 2; // bits per sample	(0010)
	ptr += length2 - 16;				          // get past the wave format chunk

	// are we in the 'data' chunk? if not, skip this chunk.
	if (ptr[0] != 'd' || ptr[1] != 'a' || ptr[2] != 't' || ptr[3] != 'a') {
		                                ptr += 4; // skip tag
		UINT32 length3 = get_long();    ptr += 4;
		ptr += length3;
	}

	/* "data" */				        ptr += 4; // "data"
	UINT32 data_length = get_long();	ptr += 4; // should be up to the data...

	if ((len - (ptr - src)) < data_length) data_length = len - (ptr - src);

	UINT32 converted_len = (UINT32)((float)(data_length * (nBurnSoundRate * 1.00000 / sample_rate) / (bits * channels)));
	if (converted_len == 0) return; 

	sample_ptr->data = (UINT8*)BurnMalloc(converted_len * 4);

	// up/down sample everything and convert to raw 16 bit stereo
	INT16 *data = (INT16*)sample_ptr->data;
	INT16 *poin = (INT16*)ptr;
	UINT8 *poib = ptr;

	if ((INT32)sample_rate == nBurnSoundRate)
	{
		// don't try to interpolate, just copy
		bprintf(0, _T("Sample at native rate already..\n"));
		for (UINT32 i = 0; i < converted_len; i++)
		{
			if (bits == 2)											//  signed 16 bit, stereo & mono
			{
				data[i * 2 + 0] = poin[i * channels + 0             ];
				data[i * 2 + 1] = poin[i * channels + (channels / 2)];
			}
			else if (bits == 1)										// unsigned 8 bit, stereo & mono
			{
				data[i * 2 + 0] = (poib[i * channels + 0             ] - 128) << 8; data[i * 2 + 0] |= (data[i * 2 + 0] >> 7) & 0xFF;
				data[i * 2 + 1] = (poib[i * channels + (channels / 2)] - 128) << 8; data[i * 2 + 1] |= (data[i * 2 + 1] >> 7) & 0xFF;
			}
		}
	}
	else
	{
		// interpolate sample
		bprintf(0, _T("Converting %dhz [%d bit, %d channels] to %dhz (native).\n"), sample_rate, bits*8, channels, nBurnSoundRate);
		INT32 buffer_l[4];
		INT32 buffer_r[4];

		memset(buffer_l, 0, sizeof(buffer_l));
		memset(buffer_r, 0, sizeof(buffer_r));

		if (sample_ptr->flags & SAMPLE_AUTOLOOP)
		{
			UINT8* end = sample_ptr->data + data_length / (bits * channels);

			if (bits == 1)
			{
				buffer_l[1] = (INT16)((*(end - 3 * channels)) - 0x80) << 8; buffer_l[1] |= (buffer_l[1] >> 7) & 0xFF;
				buffer_l[2] = (INT16)((*(end - 2 * channels)) - 0x80) << 8; buffer_l[2] |= (buffer_l[2] >> 7) & 0xFF;
				buffer_l[3] = (INT16)((*(end - 1 * channels)) - 0x80) << 8; buffer_l[3] |= (buffer_l[3] >> 7) & 0xFF;

				buffer_r[1] = (INT16)((*(end - 3 * channels) + (channels / 2)) - 0x80) << 8; buffer_r[1] |= (buffer_r[1] >> 7) & 0xFF;
				buffer_r[2] = (INT16)((*(end - 2 * channels) + (channels / 2)) - 0x80) << 8; buffer_r[2] |= (buffer_r[2] >> 7) & 0xFF;
				buffer_r[3] = (INT16)((*(end - 1 * channels) + (channels / 2)) - 0x80) << 8; buffer_r[3] |= (buffer_r[3] >> 7) & 0xFF;
			}
			else
			{
				buffer_l[1] = (INT16)(get_shorti(end - 6 * channels));
				buffer_l[2] = (INT16)(get_shorti(end - 4 * channels));
				buffer_l[3] = (INT16)(get_shorti(end - 2 * channels));

				buffer_r[1] = (INT16)(get_shorti(end - 6 * channels) + (channels & 2));
				buffer_r[2] = (INT16)(get_shorti(end - 4 * channels) + (channels & 2));
				buffer_r[3] = (INT16)(get_shorti(end - 2 * channels) + (channels & 2));
			}
		}

		UINT64 prev_offs = ~0;

		for (UINT64 i = 0; i < converted_len; i++)
		{
			UINT64 pos = (i * sample_rate << 12) / nBurnSoundRate;
			UINT64 curr_offs = pos >> 12;

			while (prev_offs != curr_offs)
			{
				prev_offs += 1;
				buffer_l[0] = buffer_l[1]; buffer_r[0] = buffer_r[1];
				buffer_l[1] = buffer_l[2]; buffer_r[1] = buffer_r[2];
				buffer_l[2] = buffer_l[3]; buffer_r[2] = buffer_r[3];

				if (bits == 2)										// signed 16 bit, stereo & mono
				{
					buffer_l[3] = (INT32)(poin[prev_offs * channels + 0             ]);
					buffer_r[3] = (INT32)(poin[prev_offs * channels + (channels / 2)]);
				}
				else if (bits == 1)									// unsigned 8 bit, stereo & mono
				{
					buffer_l[3] = (INT32)(poib[prev_offs * channels + 0             ] - 128) << 8; buffer_l[3] |= (buffer_l[3] >> 7) & 0xFF;
					buffer_r[3] = (INT32)(poib[prev_offs * channels + (channels / 2)] - 128) << 8; buffer_r[3] |= (buffer_r[3] >> 7) & 0xFF;
				}
			}

			data[i * 2 + 0] = BURN_SND_CLIP(INTERPOLATE4PS_16BIT(pos & 0x0FFF, buffer_l[0], buffer_l[1], buffer_l[2], buffer_l[3]));
			data[i * 2 + 1] = BURN_SND_CLIP(INTERPOLATE4PS_16BIT(pos & 0x0FFF, buffer_r[0], buffer_r[1], buffer_r[2], buffer_r[3]));
		}
	}

	{ // sample cleanup
		if (bBurnSampleTrimSampleEnd) { // trim silence off the end of the sample, bBurnSampleTrimSampleEnd must be set before init!
			while (data[converted_len * 2] == 0) converted_len -= 2;
		}
	}

	sample_ptr->length = converted_len;
	sample_ptr->playing = 0;
	sample_ptr->position = 0;
}

void BurnSampleInitOne(INT32); // below...

void BurnSamplePlay(INT32 sample)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_SamplesInitted) bprintf(PRINT_ERROR, _T("BurnSamplePlay called without init\n"));
#endif

	if (sample >= nTotalSamples) return;

	sample_ptr = &samples[sample];

	if (sample_ptr->flags & SAMPLE_IGNORE) return;

	if (sample_ptr->flags & SAMPLE_NOSTORE) {
		BurnSampleInitOne(sample);
	}

	sample_ptr->playing = 1;
	sample_ptr->position = 0;
}

void BurnSamplePause(INT32 sample)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_SamplesInitted) bprintf(PRINT_ERROR, _T("BurnSamplePause called without init\n"));
#endif

	if (sample >= nTotalSamples) return;

	sample_ptr = &samples[sample];
	sample_ptr->playing = 0;
}

void BurnSampleResume(INT32 sample)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_SamplesInitted) bprintf(PRINT_ERROR, _T("BurnSampleResume called without init\n"));
#endif

	if (sample >= nTotalSamples) return;

	sample_ptr = &samples[sample];
	sample_ptr->playing = 1;
}

void BurnSampleStop(INT32 sample)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_SamplesInitted) bprintf(PRINT_ERROR, _T("BurnSampleStop called without init\n"));
#endif

	if (sample >= nTotalSamples) return;

	sample_ptr = &samples[sample];
	sample_ptr->playing = 0;
	sample_ptr->position = 0;
	//sample_ptr->playback_rate = 100; // 100% // on load and reset, only!
}

void BurnSampleSetLoop(INT32 sample, bool dothis)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_SamplesInitted) bprintf(PRINT_ERROR, _T("BurnSampleSetLoop called without init\n"));
#endif

	if (sample >= nTotalSamples) return;

	sample_ptr = &samples[sample];

	sample_ptr->loop = (dothis ? 1 : 0);
}

INT32 BurnSampleGetStatus(INT32 sample)
{
	// this is also used to see if samples initialized and/or the game has samples.

	if (sample >= nTotalSamples) return -1;

	sample_ptr = &samples[sample];
	return (sample_ptr->playing);
}

INT32 BurnSampleGetPosition(INT32 sample)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_SamplesInitted) bprintf(PRINT_ERROR, _T("BurnSampleGetPosition called without init\n"));
#endif

	if (sample >= nTotalSamples) return -1;

	sample_ptr = &samples[sample];
	return (sample_ptr->position / 0x10000);
}

void BurnSampleSetPosition(INT32 sample, UINT32 position)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_SamplesInitted) bprintf(PRINT_ERROR, _T("BurnSampleSetPosition called without init\n"));
#endif

	if (sample >= nTotalSamples) return;

	sample_ptr = &samples[sample];
	sample_ptr->position = position * 0x10000;
}

void BurnSampleSetPlaybackRate(INT32 sample, INT32 rate)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_SamplesInitted) bprintf(PRINT_ERROR, _T("BurnSampleSetPlaybackRate called without init\n"));
	if (rate > 5000 || rate < 0) bprintf (PRINT_ERROR, _T("BurnSampleSetPlaybackRate called with unlikely rate (%d)!\n"), rate);
#endif

	if (sample >= nTotalSamples) return;

	sample_ptr = &samples[sample];
	sample_ptr->playback_rate = rate;
}

void BurnSampleReset()
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_SamplesInitted) bprintf(PRINT_ERROR, _T("BurnSampleReset called without init\n"));
#endif

	for (INT32 i = 0; i < nTotalSamples; i++) {
		BurnSampleStop(i);
		BurnSampleSetPlaybackRate(i, 100);

		if (sample_ptr->flags & SAMPLE_AUTOLOOP) {
			BurnSampleSetLoop(i, true); // this sets the loop flag, from the driver.
		}
	}
}

INT32 __cdecl ZipLoadOneFile(char* arcName, const char* fileName, void** Dest, INT32* pnWrote);
// JHM: Not supporting wide chars
//char* TCHARToANSI(const TCHAR* pszInString, char* pszOutString, INT32 nOutSize);
//#define _TtoA(a)	TCHARToANSI(a, NULL, 0)

void BurnSampleInit(INT32 bAdd /*add samples to stream?*/)
{
	bAddToStream = bAdd;
	nTotalSamples = 0;

	DebugSnd_SamplesInitted = 1;

	if (nBurnSoundRate == 0) {
		nTotalSamples = 0;
		return;
	}

	INT32 length;
	char path[256*2];
	char setname[128];
	void *destination = NULL;
	char szTempPath[MAX_PATH];
    // JHM: Not supporting wide chars
	//sprintf(szTempPath, _TtoA(SAMPLE_DIRECTORY));
    sprintf(szTempPath, SAMPLE_DIRECTORY);
    

	// test to see if file exists
	INT32 nEnableSamples = 0;

	if (BurnDrvGetTextA(DRV_SAMPLENAME) == NULL) { // called with no samples
		nTotalSamples = 0;
		return;
	}

	strcpy(setname, BurnDrvGetTextA(DRV_SAMPLENAME));
	sprintf(path, "%s%s.zip", szTempPath, setname);
	
	FILE *test = fopen(path, "rb");
	if (test) 
	{
		nEnableSamples = 1;
		fclose(test);
	}
	
#ifdef INCLUDE_7Z_SUPPORT
	sprintf(path, "%s%s.7z", szTempPath, setname);
	
	test = fopen(path, "rb");
	if (test)
	{	
		nEnableSamples = 1;
		fclose(test);
	}
#endif
    
	if (!nEnableSamples) return;

	struct BurnSampleInfo si;
	INT32 nSampleOffset = -1;
	do {
		BurnDrvGetSampleInfo(&si, ++nSampleOffset);
		if (si.nFlags) nTotalSamples++;
	} while (si.nFlags);

	samples = (sample_format*)BurnMalloc(sizeof(sample_format) * nTotalSamples);
	memset (samples, 0, sizeof(sample_format) * nTotalSamples);

	for (INT32 i = 0; i < nTotalSamples; i++) {
		BurnDrvGetSampleInfo(&si, i);
		char *szSampleNameTmp = NULL;
		BurnDrvGetSampleName(&szSampleNameTmp, i, 0);

		sample_ptr = &samples[i];
		
		// append .wav to filename
		char szSampleName[1024];
		memset(&szSampleName, 0, sizeof(szSampleName));
		strncpy(&szSampleName[0], szSampleNameTmp, sizeof(szSampleName) - 5); // leave space for ".wav" + null, just incase!
		strcat(&szSampleName[0], ".wav");

		if (si.nFlags == 0) break;

		if (si.nFlags & SAMPLE_NOSTORE) {
			sample_ptr->flags = si.nFlags;
			sample_ptr->data = NULL;
			continue;
		}

		sprintf (path, "%s%s", szTempPath, setname);

		destination = NULL;
		length = 0;
		ZipLoadOneFile((char*)path, (const char*)szSampleName, &destination, &length);
		
		if (length) {
			sample_ptr->flags = si.nFlags;
			bprintf(0, _T("Loading \"%S\": "), szSampleName);
			make_raw((UINT8*)destination, length);
		} else {
			sample_ptr->flags = SAMPLE_IGNORE;
		}
		
		sample_ptr->gain[BURN_SND_SAMPLE_ROUTE_1] = 1.00;
		sample_ptr->gain[BURN_SND_SAMPLE_ROUTE_2] = 1.00;
		sample_ptr->output_dir[BURN_SND_SAMPLE_ROUTE_1] = BURN_SND_ROUTE_BOTH;
		sample_ptr->output_dir[BURN_SND_SAMPLE_ROUTE_2] = BURN_SND_ROUTE_BOTH;
		sample_ptr->playback_rate = 100;

		BurnFree (destination);

		BurnSetProgressRange(1.0 / nTotalSamples);
		BurnUpdateProgress((double)1.0 / i * nTotalSamples, _T("Loading samples..."), 0);
	}
}

void BurnSampleInitOne(INT32 sample)
{
	if (sample >= nTotalSamples) {
		return;
	}

	{
		struct sample_format *clr_ptr = &samples[0];

		int i = 0;
		while (i < nTotalSamples) {
			
			if (clr_ptr->data != NULL && i != sample && (clr_ptr->flags & SAMPLE_NOSTORE)) {
				BurnFree(clr_ptr->data);
				clr_ptr->playing = 0;
				clr_ptr->playback_rate = 100;
				clr_ptr->data = NULL;
			}

			clr_ptr++, i++;
		}
	}

	if ((sample_ptr->flags & SAMPLE_NOSTORE) == 0) {
		return;
	}

	INT32 length;
	char path[256];
	char setname[128];
	void *destination = NULL;
	char szTempPath[MAX_PATH];
    // JHM: Not supporting wide chars
    //sprintf(szTempPath, _TtoA(SAMPLE_DIRECTORY));
	sprintf(szTempPath, SAMPLE_DIRECTORY);

	strcpy(setname, BurnDrvGetTextA(DRV_SAMPLENAME));
	sprintf(path, "%s%s.zip", szTempPath, setname);

	struct BurnSampleInfo si;
	BurnDrvGetSampleInfo(&si, sample);
	char *szSampleNameTmp = NULL;
	BurnDrvGetSampleName(&szSampleNameTmp, sample, 0);

	sample_ptr = &samples[sample];

	// append .wav to filename
	char szSampleName[1024];
	memset(&szSampleName, 0, sizeof(szSampleName));
	strncpy(&szSampleName[0], szSampleNameTmp, sizeof(szSampleName) - 5); // leave space for ".wav" + null, just incase!
	strcat(&szSampleName[0], ".wav");

	if (sample_ptr->playing || sample_ptr->data != NULL || sample_ptr->flags == SAMPLE_IGNORE) {
		return;
	}

	sprintf (path, "%s%s", szTempPath, setname);

	destination = NULL;
	length = 0;
	ZipLoadOneFile((char*)path, (const char*)szSampleName, &destination, &length);
		
	if (length) {
		make_raw((UINT8*)destination, length);
	}

	BurnFree (destination);
}

void BurnSampleSetRoute(INT32 sample, INT32 nIndex, double nVolume, INT32 nRouteDir)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_SamplesInitted) bprintf(PRINT_ERROR, _T("BurnSampleSetRoute called without init\n"));
	if (nIndex < 0 || nIndex > 1) bprintf(PRINT_ERROR, _T("BurnSampleSetRoute called with invalid index %i\n"), nIndex);
#endif

	if (sample >= nTotalSamples) return;

	sample_ptr = &samples[sample];
	sample_ptr->gain[nIndex] = nVolume;
	sample_ptr->output_dir[nIndex] = nRouteDir;
}

void BurnSampleSetRouteAllSamples(INT32 nIndex, double nVolume, INT32 nRouteDir)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_SamplesInitted) bprintf(PRINT_ERROR, _T("BurnSampleSetRouteAllSamples called without init\n"));
	if (nIndex < 0 || nIndex > 1) bprintf(PRINT_ERROR, _T("BurnSampleSetRouteAllSamples called with invalid index %i\n"), nIndex);
#endif

	if (!nTotalSamples) return;

	for (INT32 i = 0; i < nTotalSamples; i++) {
		sample_ptr = &samples[i];
		sample_ptr->gain[nIndex] = nVolume;
		sample_ptr->output_dir[nIndex] = nRouteDir;
	}	
}

void BurnSampleExit()
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_SamplesInitted) bprintf(PRINT_ERROR, _T("BurnSampleExit called without init\n"));
#endif

	if (!DebugSnd_SamplesInitted) return;

	for (INT32 i = 0; i < nTotalSamples; i++) {
		sample_ptr = &samples[i];
		if (sample_ptr)
			BurnFree (sample_ptr->data);
	}

	if (samples)
		BurnFree (samples);

	sample_ptr = NULL;
	nTotalSamples = 0;
	bAddToStream = 0;
	bBurnSampleTrimSampleEnd = 0;

	DebugSnd_SamplesInitted = 0;
}

void BurnSampleRender(INT16 *pDest, UINT32 pLen)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_SamplesInitted) bprintf(PRINT_ERROR, _T("BurnSampleRender called without init\n"));
#endif

	if (pBurnSoundOut == NULL) {
		return;
	}

	// if the sample player is the only, or the first, sound chip, clear out the sound buffer!
	if (bAddToStream == 0) {
		memset (pDest, 0, pLen * 2 * sizeof(INT16)); // clear buffer
	}

	for (INT32 i = 0; i < nTotalSamples; i++)
	{
		sample_ptr = &samples[i];
		if (sample_ptr->playing == 0) continue;

		INT32 playlen = pLen;
		INT32 length = sample_ptr->length;
		UINT64 pos = sample_ptr->position;
		INT32 playback_rate = (0x10000 * sample_ptr->playback_rate) / 100;

		INT16 *dst = pDest;
		INT16 *dat = (INT16*)sample_ptr->data;
		
		if (sample_ptr->loop == 0) // if not looping, check to make sure sample is in bounds
		{
			INT32 current_pos = (pos / 0x10000);
			// if sample position is greater than length, stop playback
			if ((length - current_pos) <= 0) {
				BurnSampleStop(i);
				continue;
			}

			// if samples remaining are less than playlen, set playlen to samples remaining
			if (playlen > (length - current_pos)) playlen = length - current_pos;
		}
		
		length *= 2; // (stereo) used to ensure position is within bounds
				
		for (INT32 j = 0; j < playlen; j++, dst+=2, pos+=playback_rate) {
			INT32 nLeftSample = 0, nRightSample = 0;
			UINT32 current_pos = (pos / 0x10000);
			UINT32 position = current_pos * 2; // ~1

			if (sample_ptr->loop == 0) // if not looping, check to make sure sample is in bounds
			{
				// if sample position is greater than length, stop playback
				if ((sample_ptr->length - current_pos) <= 0) {
					BurnSampleStop(i);
					break;
				}
			}

			if ((sample_ptr->output_dir[BURN_SND_SAMPLE_ROUTE_1] & BURN_SND_ROUTE_LEFT) == BURN_SND_ROUTE_LEFT) {
				nLeftSample += (INT32)(dat[(position) % length] * sample_ptr->gain[BURN_SND_SAMPLE_ROUTE_1]);
			}
			if ((sample_ptr->output_dir[BURN_SND_SAMPLE_ROUTE_1] & BURN_SND_ROUTE_RIGHT) == BURN_SND_ROUTE_RIGHT) {
				nRightSample += (INT32)(dat[(position) % length] * sample_ptr->gain[BURN_SND_SAMPLE_ROUTE_1]);
			}

			if ((sample_ptr->output_dir[BURN_SND_SAMPLE_ROUTE_2] & BURN_SND_ROUTE_LEFT) == BURN_SND_ROUTE_LEFT) {
				nLeftSample += (INT32)(dat[(position + 1) % length] * sample_ptr->gain[BURN_SND_SAMPLE_ROUTE_2]);
			}
			if ((sample_ptr->output_dir[BURN_SND_SAMPLE_ROUTE_2] & BURN_SND_ROUTE_RIGHT) == BURN_SND_ROUTE_RIGHT) {
				nRightSample += (INT32)(dat[(position + 1) % length] * sample_ptr->gain[BURN_SND_SAMPLE_ROUTE_2]);
			}

			dst[0] = BURN_SND_CLIP(nLeftSample + dst[0]);
			dst[1] = BURN_SND_CLIP(nRightSample + dst[1]);
		}

		sample_ptr->position = pos; // store the updated position 
	}
}

void BurnSampleScan(INT32 nAction, INT32 *pnMin)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_SamplesInitted) bprintf(PRINT_ERROR, _T("BurnSampleScan called without init\n"));
#endif

	if (pnMin != NULL) {
		*pnMin = 0x029707;
	}

	if (nAction & ACB_DRIVER_DATA) {
		for (INT32 i = 0; i < nTotalSamples; i++) {
			sample_ptr = &samples[i];
			SCAN_VAR(sample_ptr->playing);
			SCAN_VAR(sample_ptr->loop);
			SCAN_VAR(sample_ptr->position);
			SCAN_VAR(sample_ptr->playback_rate);
		}
	}
}
