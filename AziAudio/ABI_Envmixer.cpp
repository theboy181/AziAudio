/****************************************************************************
*                                                                           *
* Azimer's HLE Audio Plugin for Project64 Compatible N64 Emulators          *
* http://www.apollo64.com/                                                  *
* Copyright (C) 2000-2015 Azimer. All rights reserved.                      *
*                                                                           *
* License:                                                                  *
* GNU/GPLv2 http://www.gnu.org/licenses/gpl-2.0.html                        *
*                                                                           *
****************************************************************************/

#include "audiohle.h"

s16 Vol_Left;		// 0x0006(T8)
s16 Vol_Right;		// 0x0008(T8)
s16 VolTrg_Left;	// 0x0010(T8)
s32 VolRamp_Left;	// m_LeftVolTarget
//u16 VolRate_Left;	// m_LeftVolRate
s16 VolTrg_Right;	// m_RightVol
s32 VolRamp_Right;	// m_RightVolTarget
//u16 VolRate_Right;	// m_RightVolRate
s16 Env_Dry;		// 0x001C(T8)
s16 Env_Wet;		// 0x001E(T8)

static inline s32 mixer_macc(s32* Acc, s32* AdderStart, s32* AdderEnd, s32 Ramp)
{
	s64 product, product_shifted;
	s32 volume;

#if 1
	/*** TODO!  It looks like my C translation of Azimer's assembly code ... ***/
	product = (s64)(*AdderEnd) * (s64)Ramp;
	product_shifted = product >> 16;

	volume = (*AdderEnd - *AdderStart) / 8;
	*Acc = *AdderStart;
	*AdderStart = *AdderEnd;
	*AdderEnd = (s32)(product_shifted & 0xFFFFFFFFul);
#else
	/*** ... comes out to something not the same as the C code he commented. ***/
	volume = (*AdderEnd - *AdderStart) >> 3;
	*Acc = *AdderStart;
	*AdderEnd = ((s64)(*AdderEnd) * (s64)Ramp) >> 16;
	*AdderStart = ((s64)(*Acc)      * (s64)Ramp) >> 16;
#endif
	return (volume);
}

static u16 env[8];
static u32 t3, s5, s6;

void ENVSETUP1() {
	u32 tmp;

	//fprintf (dfile, "ENVSETUP1: k0 = %08X, t9 = %08X\n", k0, t9);
	t3 = k0 & 0xFFFF;
	tmp = (k0 >> 0x8) & 0xFF00;
	env[4] = (u16)tmp;
	tmp += t3;
	env[5] = (u16)tmp;
	s5 = t9 >> 0x10;
	s6 = t9 & 0xFFFF;
	//fprintf (dfile, "	t3 = %X / s5 = %X / s6 = %X / env[4] = %X / env[5] = %X\n", t3, s5, s6, env[4], env[5]);
}

void ENVSETUP2() {
	u32 tmp;

	//fprintf (dfile, "ENVSETUP2: k0 = %08X, t9 = %08X\n", k0, t9);
	tmp = (t9 >> 0x10);
	env[0] = (u16)tmp;
	tmp += s5;
	env[1] = (u16)tmp;
	tmp = t9 & 0xffff;
	env[2] = (u16)tmp;
	tmp += s6;
	env[3] = (u16)tmp;
	//fprintf (dfile, "	env[0] = %X / env[1] = %X / env[2] = %X / env[3] = %X\n", env[0], env[1], env[2], env[3]);
}

void ENVMIXER() {
	//static int envmixcnt = 0;
	u8 flags = (u8)((k0 >> 16) & 0xff);
	u32 addy = (t9 & 0xFFFFFF);// + SEGMENTS[(t9>>24)&0xf];
	//static 
	// ********* Make sure these conditions are met... ***********
	/*if ((AudioInBuffer | AudioOutBuffer | AudioAuxA | AudioAuxC | AudioAuxE | AudioCount) & 0x3) {
	MessageBox (NULL, "Unaligned EnvMixer... please report this to Azimer with the following information: RomTitle, Place in the rom it occurred, and any save state just before the error", "AudioHLE Error", MB_OK);
	}*/
	// ------------------------------------------------------------
	short *inp = (short *)(BufferSpace + AudioInBuffer);
	short *out = (short *)(BufferSpace + AudioOutBuffer);
	short *aux1 = (short *)(BufferSpace + AudioAuxA);
	short *aux2 = (short *)(BufferSpace + AudioAuxC);
	short *aux3 = (short *)(BufferSpace + AudioAuxE);
	s32 MainR;
	s32 MainL;
	s32 AuxR;
	s32 AuxL;
	int i1, o1, a1, a2, a3;
	WORD AuxIncRate = 1;
	short zero[8];
	memset(zero, 0, 16);
	s32 LVol, RVol;
	s32 LAcc, RAcc;
	s32 LTrg, RTrg;
	s16 Wet, Dry;
	u32 ptr = 0;
	s32 RRamp, LRamp;
	s32 LAdderStart, RAdderStart, LAdderEnd, RAdderEnd;
	s32 oMainR, oMainL, oAuxR, oAuxL;

	//envmixcnt++;

	//fprintf (dfile, "\n----------------------------------------------------\n");
	if (flags & A_INIT) {
		LVol = ((Vol_Left * (s32)VolRamp_Left));
		RVol = ((Vol_Right * (s32)VolRamp_Right));
		Wet = (s16)Env_Wet; Dry = (s16)Env_Dry; // Save Wet/Dry values
		LTrg = (VolTrg_Left << 16); RTrg = (VolTrg_Right << 16); // Save Current Left/Right Targets
		LAdderStart = Vol_Left << 16;
		RAdderStart = Vol_Right << 16;
		LAdderEnd = LVol;
		RAdderEnd = RVol;
		RRamp = VolRamp_Right;
		LRamp = VolRamp_Left;
	}
	else {
		// Load LVol, RVol, LAcc, and RAcc (all 32bit)
		// Load Wet, Dry, LTrg, RTrg
		memcpy((u8 *)hleMixerWorkArea, (rdram + addy), 80);
		Wet = *(s16 *)(hleMixerWorkArea + 0); // 0-1
		Dry = *(s16 *)(hleMixerWorkArea + 2); // 2-3
		LTrg = *(s32 *)(hleMixerWorkArea + 4); // 4-5
		RTrg = *(s32 *)(hleMixerWorkArea + 6); // 6-7
		LRamp = *(s32 *)(hleMixerWorkArea + 8); // 8-9 (hleMixerWorkArea is a 16bit pointer)
		RRamp = *(s32 *)(hleMixerWorkArea + 10); // 10-11
		LAdderEnd = *(s32 *)(hleMixerWorkArea + 12); // 12-13
		RAdderEnd = *(s32 *)(hleMixerWorkArea + 14); // 14-15
		LAdderStart = *(s32 *)(hleMixerWorkArea + 16); // 12-13
		RAdderStart = *(s32 *)(hleMixerWorkArea + 18); // 14-15
	}

	if (!(flags&A_AUX)) {
		AuxIncRate = 0;
		aux2 = aux3 = zero;
	}

	oMainL = (Dry * (LTrg >> 16) + 0x4000) >> 15;
	oAuxL = (Wet * (LTrg >> 16) + 0x4000) >> 15;
	oMainR = (Dry * (RTrg >> 16) + 0x4000) >> 15;
	oAuxR = (Wet * (RTrg >> 16) + 0x4000) >> 15;

	for (int y = 0; y < AudioCount; y += 0x10) {

		if (LAdderStart != LTrg) {
			LVol = mixer_macc(&LAcc, &LAdderStart, &LAdderEnd, LRamp);
		}
		else {
			LAcc = LTrg;
			LVol = 0;
		}

		if (RAdderStart != RTrg) {
			RVol = mixer_macc(&RAcc, &RAdderStart, &RAdderEnd, RRamp);
		}
		else {
			RAcc = RTrg;
			RVol = 0;
		}

		for (int x = 0; x < 8; x++) {
			i1 = (int)inp[ptr ^ 1];
			o1 = (int)out[ptr ^ 1];
			a1 = (int)aux1[ptr ^ 1];
			if (AuxIncRate) {
				a2 = (int)aux2[ptr ^ 1];
				a3 = (int)aux3[ptr ^ 1];
			}
			// TODO: here...
			//LAcc = LTrg;
			//RAcc = RTrg;

			LAcc += LVol;
			RAcc += RVol;

			if (LVol <= 0) { // Decrementing
				if (LAcc < LTrg) {
					LAcc = LTrg;
					LAdderStart = LTrg;
					MainL = oMainL;
					AuxL = oAuxL;
				}
				else {
					MainL = (Dry * ((s32)LAcc >> 16) + 0x4000) >> 15;
					AuxL = (Wet * ((s32)LAcc >> 16) + 0x4000) >> 15;
				}
			}
			else {
				if (LAcc > LTrg) {
					LAcc = LTrg;
					LAdderStart = LTrg;
					MainL = oMainL;
					AuxL = oAuxL;
				}
				else {
					MainL = (Dry * ((s32)LAcc >> 16) + 0x4000) >> 15;
					AuxL = (Wet * ((s32)LAcc >> 16) + 0x4000) >> 15;
				}
			}

			if (RVol <= 0) { // Decrementing
				if (RAcc < RTrg) {
					RAcc = RTrg;
					RAdderStart = RTrg;
					MainR = oMainR;
					AuxR = oAuxR;
				}
				else {
					MainR = (Dry * ((s32)RAcc >> 16) + 0x4000) >> 15;
					AuxR = (Wet * ((s32)RAcc >> 16) + 0x4000) >> 15;
				}
			}
			else {
				if (RAcc > RTrg) {
					RAcc = RTrg;
					RAdderStart = RTrg;
					MainR = oMainR;
					AuxR = oAuxR;
				}
				else {
					MainR = (Dry * ((s32)RAcc >> 16) + 0x4000) >> 15;
					AuxR = (Wet * ((s32)RAcc >> 16) + 0x4000) >> 15;
				}
			}

			//fprintf (dfile, "%04X ", (LAcc>>16));

			/*MainL = (((s64)Dry*2 * (s64)(LAcc>>16)) + 0x8000) >> 16;
			MainR = (((s64)Dry*2 * (s64)(RAcc>>16)) + 0x8000) >> 16;
			AuxL  = (((s64)Wet*2 * (s64)(LAcc>>16)) + 0x8000) >> 16;
			AuxR  = (((s64)Wet*2 * (s64)(RAcc>>16)) + 0x8000) >> 16;*/
			/*
			MainL = pack_signed(MainL);
			MainR = pack_signed(MainR);
			AuxL = pack_signed(AuxL);
			AuxR = pack_signed(AuxR);*/
			/*
			MainR = (Dry * RTrg + 0x10000) >> 15;
			MainL = (Dry * LTrg + 0x10000) >> 15;
			AuxR  = (Wet * RTrg + 0x8000)  >> 16;
			AuxL  = (Wet * LTrg + 0x8000)  >> 16;*/

			o1 += (/*(o1*0x7fff)+*/(i1*MainR) + 0x4000) >> 15;
			a1 += (/*(a1*0x7fff)+*/(i1*MainL) + 0x4000) >> 15;

			/*		o1=((s64)(((s64)o1*0xfffe)+((s64)i1*MainR*2)+0x8000)>>16);

			a1=((s64)(((s64)a1*0xfffe)+((s64)i1*MainL*2)+0x8000)>>16);*/

			o1 = pack_signed(o1);
			a1 = pack_signed(a1);

			out[ptr ^ 1] = o1;
			aux1[ptr ^ 1] = a1;
			if (AuxIncRate) {
				//a2=((s64)(((s64)a2*0xfffe)+((s64)i1*AuxR*2)+0x8000)>>16);

				//a3=((s64)(((s64)a3*0xfffe)+((s64)i1*AuxL*2)+0x8000)>>16);
				a2 += (/*(a2*0x7fff)+*/(i1*AuxR) + 0x4000) >> 15;
				a3 += (/*(a3*0x7fff)+*/(i1*AuxL) + 0x4000) >> 15;

				a2 = pack_signed(a2);
				a3 = pack_signed(a3);

				aux2[ptr ^ 1] = a2;
				aux3[ptr ^ 1] = a3;
			}
			ptr++;
		}
	}

	/*LAcc = LAdderEnd;
	RAcc = RAdderEnd;*/

	*(s16 *)(hleMixerWorkArea + 0) = Wet; // 0-1
	*(s16 *)(hleMixerWorkArea + 2) = Dry; // 2-3
	*(s32 *)(hleMixerWorkArea + 4) = LTrg; // 4-5
	*(s32 *)(hleMixerWorkArea + 6) = RTrg; // 6-7
	*(s32 *)(hleMixerWorkArea + 8) = LRamp; // 8-9 (hleMixerWorkArea is a 16bit pointer)
	*(s32 *)(hleMixerWorkArea + 10) = RRamp; // 10-11
	*(s32 *)(hleMixerWorkArea + 12) = LAdderEnd; // 12-13
	*(s32 *)(hleMixerWorkArea + 14) = RAdderEnd; // 14-15
	*(s32 *)(hleMixerWorkArea + 16) = LAdderStart; // 12-13
	*(s32 *)(hleMixerWorkArea + 18) = RAdderStart; // 14-15
	memcpy(rdram + addy, (u8 *)hleMixerWorkArea, 80);
}

void ENVMIXER2() {
	//fprintf (dfile, "ENVMIXER: k0 = %08X, t9 = %08X\n", k0, t9);

	s16 *bufft6, *bufft7, *buffs0, *buffs1;
	s16 *buffs3;
	s32 count;
	u32 adder;
	int x;

	s16 vec9, vec10;

	s16 v2[8];

	//assert(0);

	buffs3 = (s16 *)(BufferSpace + ((k0 >> 0x0c) & 0x0ff0));
	bufft6 = (s16 *)(BufferSpace + ((t9 >> 0x14) & 0x0ff0));
	bufft7 = (s16 *)(BufferSpace + ((t9 >> 0x0c) & 0x0ff0));
	buffs0 = (s16 *)(BufferSpace + ((t9 >> 0x04) & 0x0ff0));
	buffs1 = (s16 *)(BufferSpace + ((t9 << 0x04) & 0x0ff0));


	v2[0] = 0 - (s16)((k0 & 0x2) >> 1);
	v2[1] = 0 - (s16)((k0 & 0x1));
	v2[2] = 0 - (s16)((k0 & 0x8) >> 1);
	v2[3] = 0 - (s16)((k0 & 0x4) >> 1);

	count = (k0 >> 8) & 0xff;

	if (!isMKABI) {
		s5 *= 2; s6 *= 2; t3 *= 2;
		adder = 0x10;
	}
	else {
		k0 = 0;
		adder = 0x8;
		t3 = 0;
	}


	while (count > 0) {
		int temp;
		for (x = 0; x < 0x8; x++) {
			vec9 = (s16)(((s32)buffs3[x ^ 1] * (u32)env[0]) >> 0x10) ^ v2[0];
			vec10 = (s16)(((s32)buffs3[x ^ 1] * (u32)env[2]) >> 0x10) ^ v2[1];
			temp = bufft6[x ^ 1] + vec9;
			temp = pack_signed(temp);
			bufft6[x ^ 1] = temp;
			temp = bufft7[x ^ 1] + vec10;
			temp = pack_signed(temp);
			bufft7[x ^ 1] = temp;
			vec9 = (s16)(((s32)vec9  * (u32)env[4]) >> 0x10) ^ v2[2];
			vec10 = (s16)(((s32)vec10 * (u32)env[4]) >> 0x10) ^ v2[3];
			if (k0 & 0x10) {
				temp = buffs0[x ^ 1] + vec10;
				temp = pack_signed(temp);
				buffs0[x ^ 1] = temp;
				temp = buffs1[x ^ 1] + vec9;
				temp = pack_signed(temp);
				buffs1[x ^ 1] = temp;
			}
			else {
				temp = buffs0[x ^ 1] + vec9;
				temp = pack_signed(temp);
				buffs0[x ^ 1] = temp;
				temp = buffs1[x ^ 1] + vec10;
				temp = pack_signed(temp);
				buffs1[x ^ 1] = temp;
			}
		}

		if (!isMKABI)
		for (x = 0x8; x < 0x10; x++) {
			vec9 = (s16)(((s32)buffs3[x ^ 1] * (u32)env[1]) >> 0x10) ^ v2[0];
			vec10 = (s16)(((s32)buffs3[x ^ 1] * (u32)env[3]) >> 0x10) ^ v2[1];
			temp = bufft6[x ^ 1] + vec9;
			temp = pack_signed(temp);
			bufft6[x ^ 1] = temp;
			temp = bufft7[x ^ 1] + vec10;
			temp = pack_signed(temp);
			bufft7[x ^ 1] = temp;
			vec9 = (s16)(((s32)vec9  * (u32)env[5]) >> 0x10) ^ v2[2];
			vec10 = (s16)(((s32)vec10 * (u32)env[5]) >> 0x10) ^ v2[3];
			if (k0 & 0x10) {
				temp = buffs0[x ^ 1] + vec10;
				temp = pack_signed(temp);
				buffs0[x ^ 1] = temp;
				temp = buffs1[x ^ 1] + vec9;
				temp = pack_signed(temp);
				buffs1[x ^ 1] = temp;
			}
			else {
				temp = buffs0[x ^ 1] + vec9;
				temp = pack_signed(temp);
				buffs0[x ^ 1] = temp;
				temp = buffs1[x ^ 1] + vec10;
				temp = pack_signed(temp);
				buffs1[x ^ 1] = temp;
			}
		}
		bufft6 += adder; bufft7 += adder;
		buffs0 += adder; buffs1 += adder;
		buffs3 += adder; count -= adder;
		env[0] += (u16)s5; env[1] += (u16)s5;
		env[2] += (u16)s6; env[3] += (u16)s6;
		env[4] += (u16)t3; env[5] += (u16)t3;
	}
}

void ENVMIXER3() {
	u8 flags = (u8)((k0 >> 16) & 0xff);
	u32 addy = (t9 & 0xFFFFFF);

	short *inp = (short *)(BufferSpace + 0x4F0);
	short *out = (short *)(BufferSpace + 0x9D0);
	short *aux1 = (short *)(BufferSpace + 0xB40);
	short *aux2 = (short *)(BufferSpace + 0xCB0);
	short *aux3 = (short *)(BufferSpace + 0xE20);
	s32 MainR;
	s32 MainL;
	s32 AuxR;
	s32 AuxL;
	int i1, o1, a1, a2, a3;
	WORD AuxIncRate = 1;
	short zero[8];
	memset(zero, 0, 16);

	s32 LAdder, LAcc, LVol;
	s32 RAdder, RAcc, RVol;
	s16 RSig, LSig; // Most significant part of the Ramp Value
	s16 Wet, Dry;
	s16 LTrg, RTrg;

	Vol_Right = (*(s16 *)&k0);

	if (flags & A_INIT) {
		LAdder = VolRamp_Left / 8;
		LAcc = 0;
		LVol = Vol_Left;
		LSig = (s16)(VolRamp_Left >> 16);

		RAdder = VolRamp_Right / 8;
		RAcc = 0;
		RVol = Vol_Right;
		RSig = (s16)(VolRamp_Right >> 16);

		Wet = (s16)Env_Wet; Dry = (s16)Env_Dry; // Save Wet/Dry values
		LTrg = VolTrg_Left; RTrg = VolTrg_Right; // Save Current Left/Right Targets
	}
	else {
		memcpy((u8 *)hleMixerWorkArea, rdram + addy, 80);
		Wet = *(s16 *)(hleMixerWorkArea + 0); // 0-1
		Dry = *(s16 *)(hleMixerWorkArea + 2); // 2-3
		LTrg = *(s16 *)(hleMixerWorkArea + 4); // 4-5
		RTrg = *(s16 *)(hleMixerWorkArea + 6); // 6-7
		LAdder = *(s32 *)(hleMixerWorkArea + 8); // 8-9 (hleMixerWorkArea is a 16bit pointer)
		RAdder = *(s32 *)(hleMixerWorkArea + 10); // 10-11
		LAcc = *(s32 *)(hleMixerWorkArea + 12); // 12-13
		RAcc = *(s32 *)(hleMixerWorkArea + 14); // 14-15
		LVol = *(s32 *)(hleMixerWorkArea + 16); // 16-17
		RVol = *(s32 *)(hleMixerWorkArea + 18); // 18-19
		LSig = *(s16 *)(hleMixerWorkArea + 20); // 20-21
		RSig = *(s16 *)(hleMixerWorkArea + 22); // 22-23
	}


	//if(!(flags&A_AUX)) {
	//	AuxIncRate=0;
	//	aux2=aux3=zero;
	//}

	for (int y = 0; y < (0x170 / 2); y++) {

		// Left
		LAcc += LAdder;
		LVol += (LAcc >> 16);
		LAcc &= 0xFFFF;

		// Right
		RAcc += RAdder;
		RVol += (RAcc >> 16);
		RAcc &= 0xFFFF;
		// ****************************************************************
		// Clamp Left
		if (LSig >= 0) { // VLT
			if (LVol > LTrg) {
				LVol = LTrg;
			}
		}
		else { // VGE
			if (LVol < LTrg) {
				LVol = LTrg;
			}
		}

		// Clamp Right
		if (RSig >= 0) { // VLT
			if (RVol > RTrg) {
				RVol = RTrg;
			}
		}
		else { // VGE
			if (RVol < RTrg) {
				RVol = RTrg;
			}
		}
		// ****************************************************************
		MainL = ((Dry * LVol) + 0x4000) >> 15;
		MainR = ((Dry * RVol) + 0x4000) >> 15;

		o1 = out[y ^ 1];
		a1 = aux1[y ^ 1];
		i1 = inp[y ^ 1];

		o1 += ((i1*MainL) + 0x4000) >> 15;
		a1 += ((i1*MainR) + 0x4000) >> 15;

		// ****************************************************************

		o1 = pack_signed(o1);
		a1 = pack_signed(a1);

		// ****************************************************************

		out[y ^ 1] = o1;
		aux1[y ^ 1] = a1;

		// ****************************************************************
		//if (!(flags&A_AUX)) {
		a2 = aux2[y ^ 1];
		a3 = aux3[y ^ 1];

		AuxL = ((Wet * LVol) + 0x4000) >> 15;
		AuxR = ((Wet * RVol) + 0x4000) >> 15;

		a2 += ((i1*AuxL) + 0x4000) >> 15;
		a3 += ((i1*AuxR) + 0x4000) >> 15;

		a2 = pack_signed(a2);
		a3 = pack_signed(a3);

		aux2[y ^ 1] = a2;
		aux3[y ^ 1] = a3;
	}
	//}

	*(s16 *)(hleMixerWorkArea + 0) = Wet; // 0-1
	*(s16 *)(hleMixerWorkArea + 2) = Dry; // 2-3
	*(s16 *)(hleMixerWorkArea + 4) = LTrg; // 4-5
	*(s16 *)(hleMixerWorkArea + 6) = RTrg; // 6-7
	*(s32 *)(hleMixerWorkArea + 8) = LAdder; // 8-9 (hleMixerWorkArea is a 16bit pointer)
	*(s32 *)(hleMixerWorkArea + 10) = RAdder; // 10-11
	*(s32 *)(hleMixerWorkArea + 12) = LAcc; // 12-13
	*(s32 *)(hleMixerWorkArea + 14) = RAcc; // 14-15
	*(s32 *)(hleMixerWorkArea + 16) = LVol; // 16-17
	*(s32 *)(hleMixerWorkArea + 18) = RVol; // 18-19
	*(s16 *)(hleMixerWorkArea + 20) = LSig; // 20-21
	*(s16 *)(hleMixerWorkArea + 22) = RSig; // 22-23
	//*(u32 *)(hleMixerWorkArea + 24) = 0x13371337; // 22-23
	memcpy(rdram + addy, (u8 *)hleMixerWorkArea, 80);
}

void SETVOL() {
	// Might be better to unpack these depending on the flags...
	u8 flags = (u8)((k0 >> 16) & 0xff);
	u16 vol = (s16)(k0 & 0xffff);
	u16 voltarg = (u16)((t9 >> 16) & 0xffff);
	u16 volrate = (u16)((t9 & 0xffff));

	if (flags & A_AUX) {
		Env_Dry = (s16)vol;			// m_MainVol
		Env_Wet = (s16)volrate;		// m_AuxVol
		return;
	}

	if (flags & A_VOL) { // Set the Source(start) Volumes
		if (flags & A_LEFT) {
			Vol_Left = (s16)vol;	// m_LeftVolume
		}
		else { // A_RIGHT
			Vol_Right = (s16)vol;	// m_RightVolume
		}
		return;
	}

	//0x370				Loop Value (shared location)
	//0x370				Target Volume (Left)
	//u16 VolRamp_Left;	// 0x0012(T8)
	if (flags & A_LEFT) { // Set the Ramping values Target, Ramp
		//loopval = (((u32)vol << 0x10) | (u32)voltarg);
		VolTrg_Left = *(s16 *)&k0;		// m_LeftVol
		//VolRamp_Left = (s32)t9;
		VolRamp_Left = *(s32 *)&t9;//(u16)(t9) | (s32)(s16)(t9 << 0x10);
		//fprintf (dfile, "Ramp Left: %f\n", (float)VolRamp_Left/65536.0);
		//fprintf (dfile, "Ramp Left: %08X\n", t9);
		//VolRamp_Left = (s16)voltarg;	// m_LeftVolTarget
		//VolRate_Left = (s16)volrate;	// m_LeftVolRate
	}
	else { // A_RIGHT
		VolTrg_Right = *(s16 *)&k0;		// m_RightVol
		//VolRamp_Right = (s32)t9;
		VolRamp_Right = *(s32 *)&t9;//(u16)(t9 >> 0x10) | (s32)(s16)(t9 << 0x10);
		//fprintf (dfile, "Ramp Right: %f\n", (float)VolRamp_Right/65536.0);
		//fprintf (dfile, "Ramp Right: %08X\n", t9);
		//VolRamp_Right = (s16)voltarg;	// m_RightVolTarget
		//VolRate_Right = (s16)volrate;	// m_RightVolRate
	}
}

void SETVOL3() {
	u8 Flags = (u8)(k0 >> 0x10);
	if (Flags & 0x4) { // 288
		if (Flags & 0x2) { // 290
			Vol_Left = *(s16*)&k0; // 0x50
			Env_Dry = (s16)(*(s32*)&t9 >> 0x10); // 0x4E
			Env_Wet = *(s16*)&t9; // 0x4C
		}
		else {
			VolTrg_Right = *(s16*)&k0; // 0x46
			//VolRamp_Right = (u16)(t9 >> 0x10) | (s32)(s16)(t9 << 0x10);
			VolRamp_Right = *(s32*)&t9; // 0x48/0x4A
		}
	}
	else {
		VolTrg_Left = *(s16*)&k0; // 0x40
		VolRamp_Left = *(s32*)&t9; // 0x42/0x44
	}
}
