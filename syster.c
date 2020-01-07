/* Nagravision Syster encoder for hacktv                                 */
/*=======================================================================*/
/* Copyright 2018 Alex L. James                                          */
/* Copyright 2018 Philip Heron <phil@sanslogic.co.uk>                    */
/*                                                                       */
/* This program is free software: you can redistribute it and/or modify  */
/* it under the terms of the GNU General Public License as published by  */
/* the Free Software Foundation, either version 3 of the License, or     */
/* (at your option) any later version.                                   */
/*                                                                       */
/* This program is distributed in the hope that it will be useful,       */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/* GNU General Public License for more details.                          */
/*                                                                       */
/* You should have received a copy of the GNU General Public License     */
/* along with this program.  If not, see <http://www.gnu.org/licenses/>. */

/* -=== Nagravision Syster encoder ===-
 * 
 * These functions implement the image scrambler for Nagravision Syster.
 * This system uses line shuffling to obscure the image.
 * 
 * There is some limited support for real hardware decoders.
*/

/* -=== Discret 11 encoder ===-
 *
 * This system uses one of three line delays. 
 * Implementation here uses free access mode (audience 7),
 * which works with real D11 decoders as well as Syster decoders
 * when used with a valid card (or PIC file provided).
*/

/* Syster VBI data
 * 
 * Some or all of the notes here might be wrong. They're based on
 * data recovered from VHS recordings of Premere (Germany).
 * 
 * Data is transmitted on two VBI lines per field, 224 bits / 28 bytes
 * each encoded as NRZ. The clock rate is 284 * fH. Bytes are transmitted
 * LSB first.
 * 
 * -----------------------------------------------
 * | sync (32) | seq (8) | data (168) | crc (16) |
 * -----------------------------------------------
 * 
 * sync: 10101010 00001011 00011000 00110110 / 55 D0 18 6C
 *  seq: Hamming code sync sequence: 15 FD 73 9B 5E B6 49 A1 02 EA
 * data: Payload data (21 bytes)
 *  crc: 16-bit CRC of the 22 bytes between sync and crc
 * 
 * Blocks (packets, frames?) of 210 bytes are transmitted in 10 parts,
 * each 21 bytes long. The seq field indicates which part is currently
 * being transmitted. A block begins with seq code 15 and ends with
 * code EA, and are always transmitted in this order without interruption.
 * 
 * The decoder never activates without a key inserted. There does not appear
 * to be an equivilent to the free-access mode in Videocrypt, which works
 * with or without a card inserted.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include "video.h"
#include "vbidata.h"
#include "systerdes.h"

/* 
  * 64-bit decryption key.
  * 
  * This key is used for supplied PIC hex file.
  * This can be edited at internal EEPROM address 0x08 to 0x0F
  * in the hex file and programmed back to PIC.
  *
  * If you know the key for your official card, put it here.
 *
*/

/* 0 - 12.8 kHz complex FIR filter taps, sample rate 32 kHz */

#define NTAPS 771

static const int16_t _firi[NTAPS] = {
	0,-2,-1,-1,-2,0,-2,-1,-1,-2,0,-2,-1,-1,-2,0,-2,-1,-1,-2,0,-2,-1,-1,-2,0,-2,-1,-1,-2,0,-3,-1,-1,-3,0,-3,-1,-1,-3,0,-3,-1,-1,-3,0,-3,-1,-1,-3,0,-3,-1,-1,-4,0,-4,-1,-1,-4,0,-4,-2,-2,-4,0,-4,-2,-2,-5,0,-5,-2,-2,-5,0,-5,-2,-2,-5,0,-5,-2,-2,-6,0,-6,-2,-2,-6,0,-6,-3,-3,-7,0,-7,-3,-3,-7,0,-8,-3,-3,-8,0,-8,-3,-3,-9,0,-9,-3,-3,-9,0,-10,-4,-4,-10,0,-10,-4,-4,-11,0,-11,-4,-4,-12,0,-12,-5,-5,-12,0,-13,-5,-5,-13,0,-14,-5,-5,-14,0,-15,-6,-6,-15,0,-16,-6,-6,-16,0,-17,-6,-7,-17,0,-18,-7,-7,-19,0,-19,-7,-7,-20,0,-20,-8,-8,-21,0,-22,-8,-8,-22,0,-23,-9,-9,-24,0,-24,-9,-10,-25,0,-26,-10,-10,-27,0,-28,-11,-11,-29,0,-29,-11,-11,-30,0,-31,-12,-12,-32,0,-33,-13,-13,-34,0,-35,-14,-14,-36,0,-37,-14,-14,-39,0,-39,-15,-15,-41,0,-42,-16,-16,-43,0,-44,-17,-17,-46,0,-47,-18,-18,-49,0,-50,-19,-19,-52,0,-53,-21,-21,-55,0,-56,-22,-22,-58,0,-60,-23,-23,-62,0,-63,-25,-25,-66,0,-67,-26,-26,-70,0,-72,-28,-28,-75,0,-77,-30,-30,-80,0,-82,-32,-32,-85,0,-87,-34,-34,-91,0,-94,-36,-37,-98,0,-101,-39,-39,-105,0,-108,-42,-43,-114,0,-117,-46,-46,-123,0,-127,-50,-50,-134,0,-138,-54,-55,-146,0,-151,-59,-60,-161,0,-167,-65,-66,-178,0,-185,-73,-74,-199,0,-208,-82,-83,-224,0,-236,-93,-95,-257,0,-272,-108,-110,-300,0,-321,-128,-132,-359,0,-389,-156,-162,-447,0,-493,-200,-210,-588,0,-671,-277,-299,-857,0,-1046,-452,-513,-1573,0,-2356,-1205,-1795,-9443,-34,9427,1808,1197,2360,0,1570,516,448,1048,0,855,301,276,672,0,587,212,199,494,0,446,163,155,390,0,359,132,127,321,0,300,111,107,273,0,257,96,92,237,0,224,84,81,208,0,198,74,72,186,0,178,67,65,167,0,160,60,59,152,0,146,55,54,138,0,134,50,49,127,0,123,46,45,117,0,113,43,42,108,0,105,40,39,101,0,98,37,36,94,0,91,34,34,88,0,85,32,32,82,0,80,30,30,77,0,75,28,28,72,0,70,27,26,67,0,66,25,24,63,0,62,23,23,60,0,58,22,22,56,0,55,21,20,53,0,52,20,19,50,0,49,18,18,47,0,46,17,17,44,0,43,16,16,42,0,41,15,15,39,0,38,15,14,37,0,36,14,13,35,0,34,13,13,33,0,32,12,12,31,0,30,12,11,29,0,29,11,11,28,0,27,10,10,26,0,25,10,9,24,0,24,9,9,23,0,22,8,8,22,0,21,8,8,20,0,20,7,7,19,0,19,7,7,18,0,17,7,6,17,0,16,6,6,16,0,15,6,6,15,0,14,5,5,14,0,13,5,5,13,0,12,5,5,12,0,11,4,4,11,0,11,4,4,10,0,10,4,4,10,0,9,3,3,9,0,9,3,3,8,0,8,3,3,8,0,7,3,3,7,0,7,3,2,6,0,6,2,2,6,0,6,2,2,5,0,5,2,2,5,0,5,2,2,5,0,5,2,2,4,0,4,2,2,4,0,4,1,1,4,0,4,1,1,3,0,3,1,1,3,0,3,1,1,3,0,3,1,1,3,0,3,1,1,3,0,2,1,1,2,0,2,1,1,2,0,2,1,1,2,0,2,1,1,2,0,2,1,1,2,0,2,1,1,2,0,
};

static const int16_t _firq[NTAPS] = {
	0,-1,1,-1,1,0,-1,1,-1,1,0,-1,1,-1,1,0,-1,1,-1,1,0,-1,1,-1,1,0,-1,1,-1,1,0,-1,1,-1,1,0,-1,1,-1,1,0,-1,2,-2,1,0,-1,2,-2,1,0,-1,2,-2,1,0,-1,2,-2,1,0,-1,2,-2,1,0,-1,2,-2,1,0,-2,2,-3,2,0,-2,3,-3,2,0,-2,3,-3,2,0,-2,3,-3,2,0,-2,3,-4,2,0,-2,4,-4,2,0,-2,4,-4,3,0,-3,4,-4,3,0,-3,5,-5,3,0,-3,5,-5,3,0,-3,5,-6,3,0,-4,6,-6,4,0,-4,6,-6,4,0,-4,7,-7,4,0,-4,7,-7,5,0,-5,8,-8,5,0,-5,8,-8,5,0,-5,9,-9,6,0,-6,9,-10,6,0,-6,10,-10,6,0,-7,11,-11,7,0,-7,11,-12,7,0,-8,12,-12,8,0,-8,13,-13,8,0,-9,14,-14,9,0,-9,15,-15,9,0,-10,16,-16,10,0,-10,17,-17,10,0,-11,18,-18,11,0,-11,19,-19,12,0,-12,20,-20,12,0,-13,21,-21,13,0,-14,22,-23,14,0,-15,24,-24,15,0,-15,25,-25,16,0,-16,26,-27,17,0,-17,28,-29,18,0,-18,30,-30,19,0,-20,32,-32,20,0,-21,34,-34,21,0,-22,36,-36,23,0,-24,38,-39,24,0,-25,41,-41,26,0,-27,43,-44,27,0,-29,47,-47,29,0,-31,50,-51,32,0,-33,54,-55,34,0,-36,58,-59,37,0,-38,62,-64,40,0,-42,68,-69,43,0,-45,74,-76,47,0,-50,81,-83,52,0,-55,89,-92,57,0,-61,100,-102,64,0,-68,112,-115,72,0,-77,127,-132,83,0,-89,148,-153,97,0,-105,175,-182,116,0,-128,214,-224,144,0,-162,274,-291,189,0,-220,380,-413,276,0,-343,618,-709,507,0,-772,1650,-2485,3041,13108,3090,-2475,1656,-760,0,515,-707,621,-338,0,280,-412,381,-217,0,192,-290,275,-159,0,146,-223,214,-126,0,118,-181,175,-104,0,98,-152,148,-88,0,84,-131,128,-76,0,73,-115,112,-67,0,65,-102,100,-60,0,58,-91,90,-54,0,53,-83,81,-49,0,48,-75,74,-45,0,44,-69,68,-41,0,40,-63,63,-38,0,37,-59,58,-35,0,34,-54,54,-32,0,32,-51,50,-30,0,30,-47,47,-28,0,28,-44,44,-26,0,26,-41,41,-25,0,24,-39,38,-23,0,23,-36,36,-22,0,22,-34,34,-20,0,20,-32,32,-19,0,19,-30,30,-18,0,18,-28,28,-17,0,17,-27,27,-16,0,16,-25,25,-15,0,15,-24,24,-14,0,14,-22,22,-13,0,13,-21,21,-13,0,13,-20,20,-12,0,12,-19,19,-11,0,11,-18,18,-11,0,11,-17,17,-10,0,10,-16,16,-9,0,9,-15,15,-9,0,9,-14,14,-8,0,8,-13,13,-8,0,8,-12,12,-7,0,7,-12,12,-7,0,7,-11,11,-7,0,6,-10,10,-6,0,6,-10,10,-6,0,6,-9,9,-5,0,5,-8,8,-5,0,5,-8,8,-5,0,5,-7,7,-4,0,4,-7,7,-4,0,4,-6,6,-4,0,4,-6,6,-4,0,4,-6,5,-3,0,3,-5,5,-3,0,3,-5,5,-3,0,3,-4,4,-3,0,3,-4,4,-2,0,2,-4,4,-2,0,2,-3,3,-2,0,2,-3,3,-2,0,2,-3,3,-2,0,2,-3,3,-2,0,2,-3,2,-1,0,1,-2,2,-1,0,1,-2,2,-1,0,1,-2,2,-1,0,1,-2,2,-1,0,1,-2,2,-1,0,1,-2,2,-1,0,1,-1,1,-1,0,1,-1,1,-1,0,1,-1,1,-1,0,1,-1,1,-1,0,1,-1,1,-1,0,1,-1,1,-1,0,1,-1,1,-1,0,1,-1,1,-1,0,
};

/* 12.8 kHz complex carrier, sample rate 32 kHz */

static const int16_t _mixi[5] = { 16383, -13254, 5063, 5063, -13254 };
static const int16_t _mixq[5] = { 0, 9630, -15581, 15581, -9630 };

/* Masks for the PRBS */
#define _PRBS_SR1_MASK (((uint32_t) 1 << 31) - 1)
#define _PRBS_SR2_MASK (((uint32_t) 1 << 29) - 1)

/* The standard syster substitution table */

static const uint8_t _key_table1[0x100] = {
	10, 11, 12, 13, 16, 17, 18, 19, 13, 14, 15, 16,  0,  1,  2,  3,
	21, 22, 23, 24, 18, 19, 20, 21, 23, 24, 25, 26, 26, 27, 28, 29,
	19, 20, 21, 22, 11, 12, 13, 14, 28, 29, 30, 31,  4,  5,  6,  7,
	22, 23, 24, 25,  5,  6,  7,  8, 31,  0,  1,  2, 27, 28, 29, 30,
	 3,  4,  5,  6,  8,  9, 10, 11, 14, 15, 16, 17, 25, 26, 27, 28,
	15, 16, 17, 18,  7,  8,  9, 10, 17, 18, 19, 20, 29, 30, 31,  0,
	24, 25, 26, 27, 20, 21, 22, 23,  1,  2,  3,  4,  6,  7,  8,  9,
	12, 13, 14, 15,  9, 10, 11, 12,  2,  3,  4,  5, 30, 31,  0,  1,
	24, 25, 26, 27,  2,  3,  4,  5, 31,  0,  1,  2,  7,  8,  9, 10,
	13, 14, 15, 16, 26, 27, 28, 29, 14, 15, 16, 17, 18, 19, 20, 21,
	22, 23, 24, 25,  5,  6,  7,  8, 19, 20, 21, 22, 12, 13, 14, 15,
	17, 18, 19, 20, 27, 28, 29, 30, 10, 11, 12, 13, 11, 12, 13, 14,
	 6,  7,  8,  9,  1,  2,  3,  4,  0,  1,  2,  3,  4,  5,  6,  7,
	 3,  4,  5,  6,  8,  9, 10, 11, 15, 16, 17, 18, 23, 24, 25, 26,
	29, 30, 31,  0, 25, 26, 27, 28,  9, 10, 11, 12, 21, 22, 23, 24,
	20, 21, 22, 23, 30, 31,  0,  1, 16, 17, 18, 19, 28, 29, 30, 31
};

/* Canal+ FR (Oct 1997) */
/*static const uint8_t _key_table2[0x100] = {
	 0,  1,  2,  3,  4,  5,  6,  7,  2,  5,  4,  7,  8,  9, 10, 11,
	14, 17, 16, 19, 22, 25, 24, 27, 28, 31, 30,  1, 24, 27, 26, 29,
	 8, 11, 10, 13, 20, 23, 22, 25, 20, 21, 22, 23, 30, 31,  0,  1,
	16, 17, 18, 19, 28, 29, 30, 31, 10, 11, 12, 13, 16, 17, 18, 19,
	12, 15, 14, 17,  0,  1,  2,  3, 20, 23, 22, 25, 18, 19, 20, 21,
	22, 25, 24, 27, 26, 27, 28, 29, 18, 21, 20, 23, 10, 13, 12, 15,
	28, 29, 30, 31,  4,  5,  6,  7, 22, 23, 24, 25,  4,  7,  6,  9,
	30,  1,  0,  3, 26, 29, 28, 31,  2,  5,  4,  7,  8,  9, 10, 11,
	14, 15, 16, 17, 24, 27, 26, 29, 14, 17, 16, 19,  6,  9,  8, 11,
	16, 19, 18, 21, 28, 31, 30,  1, 24, 25, 26, 27, 20, 21, 22, 23,
	 0,  3,  2,  5,  6,  7,  8,  9, 12, 13, 14, 15,  8, 11, 10, 13,
	 2,  3,  4,  5, 30, 31,  0,  1, 24, 25, 26, 27,  2,  3,  4,  5,
	30,  1,  0,  3,  6,  9,  8, 11, 12, 15, 14, 17, 26, 27, 28, 29,
	14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  4,  7,  6,  9,
	18, 21, 20, 23, 12, 13, 14, 15, 16, 19, 18, 21, 26, 29, 28, 31,
	10, 11, 12, 13, 10, 13, 12, 15,  6,  7,  8,  9,  0,  3,  2,  5
};*/

static const uint8_t _vbi_sequence[10] = {
	0x73, 0x9B, 0x5E, 0xB6, 0x49, 0xA1, 0x02, 0xEA, 0x15, 0xFD
};

/* Blank ECM table for random control words */
static ng_ecm_t _ecm_table_rand[0x40];

static const uint8_t *_dummy_emm = (const uint8_t *) "\xFF\xFF\xFF\xFF" "DUMMYEMMDUMMYEMMDUMMYEMMDUMMYEMMDUMMYEMMDUMMYEMMDUMMYEMMDUMMYEMM" "\x9E\x4D\xDC\xF0";
static const uint8_t _ppua_emm[] = { 0x00,0x40,0x00,0x00,0x43,0x43,0x41,0x80,0x69,0x4A,0x10,0x22,0xE3,0xA9,0x9A,0xF8,0xB9,0x0F,0xD4,0xEF,0x6E,0x8A,0x30,0xCF,0xA4,0xCD,0xAD,0x83,0x4D,0xA3,0x1C,0xB0,0x2F,0x78,0xCE,0xE9,0xA8,0xDE,0xBB,0x4A,0x06,0xF0,0x27,0x4C,0xA6,0xBD,0xAD,0x67,0x9C,0xEB,0xAD,0xAE,0xD2,0xA5,0x31,0xC9,0x51,0x58,0x0D,0x72,0xF5,0x7B,0xF4,0x74,0x2D,0x45,0x3D,0xB1,0x87,0x78,0x21,0x69 };

static void _prbs_reset(ng_t *s, uint64_t cw)
{
	s->sr1 = cw & _PRBS_SR1_MASK;
	s->sr2 = (cw >> 32) & _PRBS_SR2_MASK;
}

static uint16_t _prbs_update(ng_t *s)
{
	uint16_t code = 0;
	int a, i;
	
	for(i = 0; i < 16; i++)
	{
		/* Shift the registers */
		s->sr1 = (s->sr1 >> 1) ^ (s->sr1 & 1 ? 0x7BB88888UL : 0);
		s->sr2 = (s->sr2 >> 1) ^ (s->sr2 & 1 ? 0x17A2C100UL : 0);
		
		/* Load the multiplexer address */
		a = (s->sr2 >> 24) & 0x1F;
		if(a == 31) a = 30;
		
		/* Shift into result register */
		code = (code << 1) | ((s->sr1 >> a) & 1);
	}
	
	/* Code is: rrrrrrrrsssssssx
	 * x = spare bit
	 * r = 8-bit r value
	 * s = 7-bit s value */
	
	return(code >> 1);
}

 static const int d11_lookup_table[8] = {
	 0x00, 0x01, 0x02, 0x02, 0x02, 0x00, 0x00, 0x01 
 };

static uint16_t _crc(const uint8_t *data, size_t length)
{
	uint16_t crc = 0x0000;
	const uint16_t poly = 0xC003;
	int b;
	
	while(length--)
	{
		crc ^= *(data++);
		
		for(b = 0; b < 8; b++)
		{
			crc = (crc & 1 ? (crc >> 1) ^ poly : crc >> 1);
		}
	}
	
	return(crc);
}

static void _pack_vbi_block(uint8_t vbi[10][NG_VBI_BYTES], const uint8_t msg1[NG_MSG_BYTES], const uint8_t msg2[NG_MSG_BYTES])
{
	int i, x;
	
	/* A block covers 10 VBI lines and contains various control bytes, two
	 * EMM messages, a PRBS codeword, some unknown data and two methods of
	 * error detection. */
	
	/* Copy the message data */
	memcpy(&vbi[4][5], &msg2[0], 21);
	memcpy(&vbi[5][5], &msg2[21], 21);
	memcpy(&vbi[2][5], &msg2[42], 21);
	memcpy(&vbi[3][5], &msg2[63], 21);
	
	memcpy(&vbi[8][5], &msg1[0], 21);
	memcpy(&vbi[9][5], &msg1[21], 21);
	memcpy(&vbi[6][5], &msg1[42], 21);
	memcpy(&vbi[7][5], &msg1[63], 21);
	
	/* Calculate the XOR packet data */
	for(x = 5; x < 26; x++)
	{
		vbi[0][x] = vbi[1][x] = 0x00;
		
		for(i = 2; i < 10; i++)
		{
			vbi[i & 1][x] ^= vbi[i][x];
		}
	}
	
	/* Generate the VBI header and CRC for each line */
	for(i = 0; i < 10; i++)
	{
		uint16_t crc;
		
		vbi[i][0] = 0x55;
		vbi[i][1] = 0xD0;
		vbi[i][2] = 0x18;
		vbi[i][3] = 0x6C;
		vbi[i][4] = _vbi_sequence[i];
		
		/* Calculate and apply the CRC */
		crc = _crc(&vbi[i][4], 22);
		vbi[i][26] = (crc & 0x00FF) >> 0;
		vbi[i][27] = (crc & 0xFF00) >> 8;
	}
}

void _ecm_part(ng_t *s, uint8_t *dst)
{
	const uint8_t il[20] = {
		0x00, 0x01, 0x30, 0x31, 0x40, 0x41, 0x20, 0x21, 0x60, 0x61,
		0x00, 0x01, 0x7E, 0x7F, 0x50, 0x51, 0x70, 0x71, 0x10, 0x11,
	};
	const uint8_t ap[20] = {
		0x01, 0x0F, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
		0x01, 0x00, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	};
	
	const ng_ecm_t *ecm;
	const uint8_t *d;
	uint16_t c;
	
	/* Calculate ECM table offset for this block */
	c = (s->block_seq / 20 * 2 + il[s->block_seq % 20]) & 0x7F;

	ecm = &s->blocks[c / 2];

	/* Get a pointer to the 8 ECM bytes to send */
	d = &ecm->ecm[c & 1 ? 8 : 0];
	
	/* Encode the result into the VBI line */
	c = (c << 4) | ap[s->block_seq % 20];
	dst[0] = c >> 8;
	dst[1] = c & 0xFF;
	memcpy(&dst[2], d, 8);
	
	if(ap[s->block_seq % 20] == 0x00)
	{
		s->cw = ecm->cw;
	}
	else if(s->block_seq % 20 == 13)
	{
		/* The ECM is updated here */
	}
}

static void _update_field_order(ng_t *s)
{
	int i, j;
	int b[32];
	
	/* This function generates the scrambled line order for the
	 * next field based on _key_table, s->s and s->r parameters.
	 *
	 * Based on work by Markus G. Kuhn from his publication
	 * 'Analysis of the Nagravision Video Scrambling Method', 1998-07-09
	*/
	
	for(i = 0; i < 32; i++)
	{
		b[i] = -32 + i;
	}
	
	for(i = 0; i < 287; i++)
	{
		j = i <= 254 ? _key_table1[(s->r + (2 * s->s + 1) * i) & 0xFF] : i - 255;
		b[j] = s->order[b[j] + 32] = i;
	}
}

/* 
 * This function generates the line delays for each of the 6 frames
 * within a D11 cycle period in audience 7 mode (free access).
 *
 * Most of the information has been obtained from author of CryptImage
 * http://cryptimage.vot.pl/cryptimage.php
 *
 * Additional info here:
 * https://web.archive.org/web/20180726143048/http://wintzx.fr/blog/2014/01/codage-et-decodage-des-chaines-analogiques-en-1984-partie-1/
*/

void _create_d11_delay_table(ng_t *s)
{
 /* Magic starting seed = 1337d shifted 177 times */
 int nCode = 0x672;
 int b10,b8, d11_delay_index;
 int d11_field = -1;

 for(int line = 0; line < D11_LINES_PER_FIELD * D11_FIELDS ; line++)
 {
	 if(line % D11_LINES_PER_FIELD == 0) d11_field++;

	 /* Get bit 10 */
	 b10 = ((nCode & 0x400) >> 10) & 0x01;

	 /* Get bit 8 */
	 b8  = ((nCode & 0x100) >> 8) & 0x01;

	 /* Get z bit */
	 d11_delay_index  = ((d11_field / 3) & 0x1) << 2;

	 /* Bit y b0 poly */
	 d11_delay_index |= (nCode & 0x01) << 1;

	 /* Bit x b10 poly */
	 d11_delay_index |= b10 ;

	 /* Build delay array */
	 s->d11_line_delay[line] = d11_lookup_table[d11_delay_index];

	 /* Shift along */
	 nCode = (nCode << 1);
	 nCode |= b10 ^ b8;
	 nCode &= 0x7FF;
 }
}

int _ng_vbi_init(ng_t *s, vid_t *vid)
{
	int i;
	s->vid = vid;
	
	/* Calculate the high level for the VBI data, 66% of the white level */
	i = round((vid->white_level - vid->black_level) * 0.66);
	s->lut = vbidata_init(
		NG_VBI_WIDTH, s->vid->width,
		i,
		VBIDATA_FILTER_RC, 0.7
	);
	
	if(!s->lut)
	{
		return(VID_OUT_OF_MEMORY);
	}
	
	s->vbi_seq = 0;
	s->block_seq = 0;
	
	return(VID_OK);
}

void _render_ng_vbi(ng_t *s, int line, int mode)
{
	int x;

	/* Render the VBI data
	 * These lines were used by Premiere */

	/* French C+ key lines: 13, 14, 326, 327 (ATR 18381200FF148083) (offset 1) */
    /* Premiere  key lines: 14, 15, 327, 328 (ATR 1C381405FF14E1E5) (offset 0) */
    /* Polish C+ key lines: 10, 11, 323, 324 (ATR 1CE00C01FF14E1E5) (offset 4) */
	
	if(line == 14 + s->vbioffset|| line == 15 + s->vbioffset|| line == 327 + s->vbioffset || line == 328 + s->vbioffset)
	{

		if(s->vbi_seq == 0)
		{
			const uint8_t *emm1 = _dummy_emm;
			const uint8_t *emm2 = _dummy_emm;
			uint8_t msg1[NG_MSG_BYTES];
			uint8_t msg2[NG_MSG_BYTES];
			
			/* Transmit the PPUA EMM every 1000 frames */
			if(s->vid->frame > s->next_ppua)
			{
				emm1 = _ppua_emm;
				s->next_ppua = s->vid->frame + 1000;
			}
			
			/* Build part 1 of the VBI block */
			msg1[ 0] = mode;	/* Mode? (0x72 = Premiere / Canal+ Old, 0x48 = Clear, 0x7A or 0xFA) */
			_ecm_part(s, &msg1[1]);
			msg1[ 1] |= s->audience << 3;
			msg1[11] = 0xFF;	/* Simple checksum -- the Premiere VBI sample only has 0x00/0xFF here */
			for(x = 0; x < 11; x++)
			{
				msg1[11] ^= msg1[x];
			}
			memcpy(&msg1[12], emm1, 72);
			
			/* Build part 2 of the VBI block */
			msg2[ 0] = 0xFE;	/* ??? Premiere DE: 0xFE, Canal+ PL: 0x00 */
			msg2[ 1] = 0x28;	/* ??? Premiere DE: 0x28, Canal+ PL: 0x2A */
			msg2[ 2] = 0xB1;	/* ??? Premiere DE: 0xB1, Canal+ PL: 0xE4 */
			msg2[ 3] = emm1 == _ppua_emm ? 0x01 : 0x00;	/* 0x00, or 0x01 when a broadcast EMM is present */
			msg2[ 4] = emm2 == _ppua_emm ? 0x01 : 0x00;
			msg2[ 5] = 0x00;	/* The following bytes are always 0x00 */
			msg2[ 6] = 0x00;
			msg2[ 7] = 0x00;
			msg2[ 8] = 0x00;
			msg2[ 9] = 0x00;
			msg2[10] = 0x00;
			msg2[11] = 0x00;
			memcpy(&msg2[12], emm2, 72);
			
			/* Pack the messages into the next 10 VBI lines */
			_pack_vbi_block(s->vbi, msg1, msg2);
			
			/* Advance the block sequence counter */
			s->block_seq++;
		}
		
		/* Render the line */
		vbidata_render_nrz(s->lut, s->vbi[s->vbi_seq++], -45, NG_VBI_BYTES * 8, VBIDATA_LSB_FIRST, s->vid->output, 2);
		
		if(s->vbi_seq == 10)
		{
			s->vbi_seq = 0;
		}
	}
}

int _ng_audio_init(ng_t *s)
{
	/* Allocate memory for the audio inversion FIR filters */
	s->firli = calloc(NTAPS * 2, sizeof(int16_t));
	s->firlq = calloc(NTAPS * 2, sizeof(int16_t));
	s->firri = calloc(NTAPS * 2, sizeof(int16_t));
	s->firrq = calloc(NTAPS * 2, sizeof(int16_t));
	s->mixx = 0;
	s->firx = 0;
	
	if(s->firli == NULL || s->firlq == NULL ||
		 s->firri == NULL || s->firrq == NULL)
	{
		return(VID_OUT_OF_MEMORY);
	}
	
	return(VID_OK);
}

void _rand_seed(ng_t *s, unsigned char data[8], unsigned char k64[8])
{
	int i, j;
	
	/* Generate 64 random control words */
	for(j =0 ; j < 0x40; j++)
	{
		for(i = 0; i < 16; i++)
		{
			s->blocks[j].ecm[i] = i < 4 || i > 12 ? rand() + 0xFF : data[i-4];
		}
		
		/* Encrypt plain control word */
		_get_syster_cw(s->blocks[j].ecm, k64, NG_ENCRYPT);
		
		/* Decrypt control word  */
		s->blocks[j].cw = _get_syster_cw(s->blocks[j].ecm, k64, NG_DECRYPT);
	}
}

int _init_common(ng_t *s, char *mode)
{
	int i, j;

	if(strcmp(mode, "pirate") == 0)
	{
		unsigned char k64[8] = { 0x68, 0xAE, 0x0C, 0xE9, 0x64, 0x16, 0x78, 0x8D };
		s->blocks = _ecm_table_rand;

		/* Generate 64 random control words */
		for(j =0 ; j < 0x40; j++)
		{
			for(i = 0; i < 16; i++)
			{
				s->blocks[j].ecm[i] = rand() + 0xFF;
			}
			s->blocks[j].cw = _get_syster_cw(s->blocks[j].ecm, k64, NG_DECRYPT);
		}
	}
	else if(strcmp(mode, "premiere") == 0)
	{
		s->vbioffset = 0;
		/* Free access key */
		unsigned char k64[8] = { 0xC4,0xA5,0xA8,0x18,0x74,0x93,0xC7,0x65 };
		
		/*
		   data[0] = window (operator?)
		   data[1] = channel
		   data[2] = audience (0x11 = free access)
		   data[3] = ??
		   data[4] = date
		   data[5] = date
		   data[6] = ??
		   data[7] = ??
		*/
		unsigned char data[8] = { 0xFF,0x01,0x11,0x00,0x88,0x15,0x00,0x00 };
		
		/* 
		   VBI offset:  0 = works with most keys
		   VBI offset: -2 = required for some keys
		*/
		s->vbioffset = 0;
		s->audience = data[2];
		s->blocks = _ecm_table_rand;
		
		/* Grab random seed */
		_rand_seed(s, data, k64);
	}
	else if(strcmp(mode, "cplfa") == 0)
	{		
		/* Free access key */
		unsigned char k64[8] = { 0xC4,0xA5,0xA8,0x18,0x74,0x93,0xC7,0x65 };
		
		/*
		   data[0] = window (operator?)
		   data[1] = channel
		   data[2] = audience (0x11 = free access)
		   data[3] = ??
		   data[4] = date
		   data[5] = date
		   data[6] = ??
		   data[7] = ??
		*/
		unsigned char data[8] = { 0xFF,0x05,0x11,0x00,0x88,0x15,0x00,0x00 };
		
		s->vbioffset = -4;
		s->audience = data[2];
		s->blocks = _ecm_table_rand;
		
		/* Grab random seed */
		_rand_seed(s, data, k64);
	}
	else if(strcmp(mode, "cfrfa") == 0)
	{
		/* Free access key */
		unsigned char k64[8] = { 0xC4,0xA5,0xA8,0x18,0x74,0x93,0xC7,0x65 };
		
		/*
		   data[0] = window (operator?)
		   data[1] = channel
		   data[2] = audience (0x11 = free access)
		   data[3] = ??
		   data[4] = date
		   data[5] = date
		   data[6] = ??
		   data[7] = ??
		*/
		unsigned char data[8] = { 0xFF,0x01,0x11,0x00,0x88,0x15,0x00,0x00 };
		
		/* 
		   VBI offset: -1 = old Canal+ France keys (white)
		   VBI offset: -3 = new Canal+ France keys (grey)
		*/
		s->vbioffset = -1;
		s->audience = data[2];
		s->blocks = _ecm_table_rand;
		
		/* Grab random seed */
		_rand_seed(s, data, k64);
	}
	else if(strcmp(mode, "cfrca") == 0)
	{
		/* Key 0 for operator 1 */
		unsigned char k64[8]  = { 0x00,0xAE,0x52,0x90,0x49,0xF1,0xF1,0xBB };
		
		/*
		   data[0] = window (operator?)
		   data[1] = channel
		   data[2] = audience (0x11 = free access)
		   data[3] = ??
		   data[4] = date
		   data[5] = date
		   data[6] = ??
		   data[7] = ??
		*/
		unsigned char data[8] = { 0xFF,0x01,0x01,0x00,0x7B,0x0A,0x00,0x00 };
		
		/* 
		   VBI offset: -1 = old Canal+ France keys (white)
		   VBI offset: -3 = new Canal+ France keys (grey)
		*/
		s->vbioffset = -1;
		s->audience = data[2];
		s->blocks = _ecm_table_rand;

		/* Grab random seed */
		_rand_seed(s, data, k64);
	}
	else if(strcmp(mode, "csp") == 0)
	{
		/* Free access key */
		unsigned char k64[8] = { 0xC4,0xA5,0xA8,0x18,0x74,0x93,0xC7,0x65 };

		/*
		   data[0] = window (operator?)
		   data[1] = channel
		   data[2] = audience (0x11 = free access)
		   data[3] = ??
		   data[4] = date
		   data[5] = date
		   data[6] = ??
		   data[7] = ??
		*/
		
		unsigned char data[8] = { 0x80, 0x01, 0x11, 0x00, 0x7B, 0x0A, 0x00, 0x00 };

		s->vbioffset = -4;
		s->audience = data[2];
		s->blocks = _ecm_table_rand;
		
		/* Grab random seed */
		_rand_seed(s, data, k64);
	}
	else
	{
		fprintf(stderr, "Unrecognised mode '%s'.\n", mode);
		return(VID_ERROR);
	}

	return(VID_OK);
}

int d11_init(ng_t *s, vid_t *vid, char *mode)
{
	memset(s, 0, sizeof(ng_t));
	
	if(_init_common(s, mode) == VID_ERROR)
	{
		return VID_ERROR;
	};

	s->vid = vid;
	
	/* Initialise VBI sequences - this is still necessary for D11 */
	_ng_vbi_init(s,vid);
	_ng_audio_init(s);
	
	s->d11_delay = (1 / 4433618.75) * 4 * s->vid->sample_rate;
	
	_create_d11_delay_table(s);

	return(VID_OK);
}

void d11_render_line(ng_t *s)
{
	int x, f, i, d11_field, index, line, delay;
	
	/* Calculate the field and field line */
	line = s->vid->line;
	f = (line < D11_FIELD_2_START ? 0 : 1);
	i = line - (f == 0 ? D11_FIELD_1_START : D11_FIELD_2_START);
	d11_field = (s->vid->frame % 3) + (s->vid->frame % 3) + f;
	
	if(i > 0 && i < D11_LINES_PER_FIELD)
	{
		 /* Calculate index for delay values - sequence starts on last field of the last frame */
		index = ((d11_field == 5 ? 0 : d11_field + 1) * D11_LINES_PER_FIELD) + i ;
		
		/* Calculate delay for this line */
		delay = s->d11_line_delay[index] * s->d11_delay;
	  
		/* Black level on delayed samples */
		for(x = s->vid->active_left; x < s->vid->active_left + delay; x++)
		{
				s->vid->output[x * 2 + 1] = s->vid->black_level;
		}
		
		/* Delay */
		for(; x < s->vid->active_left + s->vid->active_width; x++)
		{
			 s->vid->output[x * 2 + 1] = s->vid->output[(x - delay) * 2];
		}
		
		/* Copy delayed line to output buffer */
		for(x = s->vid->active_left; x < s->vid->active_left + s->vid->active_width; x++)
		{
			s->vid->output[x * 2] = s->vid->output[x * 2 + 1];
		}
	}
	
	/* D11 sequence sync line 622 - always white level for audience 7 mode */
	if(line == 622)
	{
			for(x = s->vid->active_left; x < s->vid->active_left + s->vid->active_width; x++)
			{
				s->vid->output[x * 2] = s->vid->white_level;
			}
	}
	
	/* D11 sequence sync line 310 - triggers white level on the last field of the last frame  */
	if(line == 310)
	{
			for(x = s->vid->active_left; x < s->vid->active_left + s->vid->active_width; x++)
			{
				s->vid->output[x * 2] = (s->vid->frame % 3 == 2 ? s->vid->white_level : s->vid->black_level);
			}
	}
	
	_render_ng_vbi(s,line,0x71);
}

int ng_init(ng_t *s, vid_t *vid, char *mode)
{
	time_t t;
	srand((unsigned) time(&t));

	memset(s, 0, sizeof(ng_t));
	
	s->vid = vid;
	s->vbioffset = 0;
	s->audience = 0x00;

	_ng_vbi_init(s,vid);
	_ng_audio_init(s);
	
	/* Initial seeds. Updated every field. */
	s->s = 0;
	s->r = 0;
	_update_field_order(s);

	if(_init_common(s, mode) == VID_ERROR)
	{
		return VID_ERROR;
	};
	
	/* Allocate memory for the delay */
	s->vid->olines += NG_DELAY_LINES;
	
	/* Allocate memory for the audio inversion FIR filters */
	s->firli = calloc(NTAPS * 2, sizeof(int16_t));
	s->firlq = calloc(NTAPS * 2, sizeof(int16_t));
	s->firri = calloc(NTAPS * 2, sizeof(int16_t));
	s->firrq = calloc(NTAPS * 2, sizeof(int16_t));
	s->mixx = 0;
	s->firx = 0;
	
	if(s->firli == NULL || s->firlq == NULL ||
	   s->firri == NULL || s->firrq == NULL)
	{
		return(VID_OUT_OF_MEMORY);
	}

	return(VID_OK);
}

void ng_free(ng_t *s)
{
	free(s->firli);
	free(s->firlq);
	free(s->firri);
	free(s->firrq);
	free(s->delay);
	free(s->lut);
}

void ng_invert_audio(ng_t *s, int16_t *audio, size_t samples)
{
	int i, x;
	int a;
	
	/* Invert the audio spectrum below 12.8 kHz.
	 * 
	 * Each audio channel is mixed with a complex sine wave to create a
	 * DSB-SC signal at +12.8 kHz. A sharp FIR filter is then used to
	 * remove everything but the lower sideband, with the result then
	 * copied back into the audio buffer.
	 * 
	 * The mixing and filtering use complex operations to avoid the
	 * upper sideband interfering after mixing.
	 */
	
	if(audio == NULL) return;
	
	for(i = 0; i < samples; i++)
	{
		/* Left */
		s->firli[s->firx + NTAPS] = s->firli[s->firx] =
			(audio[i * 2 + 0] * _mixi[s->mixx] -
			 audio[i * 2 + 0] * _mixq[s->mixx]) >> 15;
		s->firlq[s->firx + NTAPS] = s->firlq[s->firx] =
			(audio[i * 2 + 0] * _mixq[s->mixx] +
			 audio[i * 2 + 0] * _mixi[s->mixx]) >> 15;
		
		/* Right */
		s->firri[s->firx + NTAPS] = s->firri[s->firx] =
			(audio[i * 2 + 1] * _mixi[s->mixx] -
			 audio[i * 2 + 1] * _mixq[s->mixx]) >> 15;
		s->firrq[s->firx + NTAPS] = s->firrq[s->firx] =
			(audio[i * 2 + 1] * _mixq[s->mixx] +
			 audio[i * 2 + 1] * _mixi[s->mixx]) >> 15;
		
		if(++s->firx == NTAPS) s->firx = 0;
		if(++s->mixx == 5) s->mixx = 0;
		
		/* Left */
		for(a = x = 0; x < NTAPS; x++)
		{
			a += s->firli[s->firx + x] * _firi[x] -
			     s->firlq[s->firx + x] * _firq[x];
		}
		
		audio[i * 2 + 0] = a >> 15;
		
		/* Right */
		for(a = x = 0; x < NTAPS; x++)
		{
			a += s->firri[s->firx + x] * _firi[x] -
			     s->firrq[s->firx + x] * _firq[x];
		}
		
		audio[i * 2 + 1] = a >> 15;
	}
}

void ng_render_line(ng_t *s)
{
	int j = 0;
	int x, f, i;
	int line;
	
	/* Calculate which line is about to be transmitted due to the delay */
	line = s->vid->line - NG_DELAY_LINES;
	if(line < 0) line += s->vid->conf.lines;
	
	/* Calculate the field and field line */
	f = (line < NG_FIELD_2_START ? 1 : 2);
	i = line - (f == 1 ? NG_FIELD_1_START : NG_FIELD_2_START);
	
	if(i >= 0 && i < NG_LINES_PER_FIELD)
	{
		/* Adjust for the decoder's 32 line delay */
		i += 32;
		if(i >= NG_LINES_PER_FIELD)
		{
			i -= NG_LINES_PER_FIELD;
			f = (f == 1 ? 2 : 1);
		}
		
		/* Reinitialise the seeds if this is a new field */
		if(i == 0)
		{
			int sf = s->vid->frame % 50;
			
			if((sf == 6 || sf == 31) && f == 2)
			{
				_prbs_reset(s, s->cw);
			}
			
			x = _prbs_update(s);
			
			s->s = x & 0x7F;
			s->r = x >> 7;
			
			_update_field_order(s);
		}
		
		/* Calculate which line in the delay buffer to copy image data from */
		j = (f == 1 ? NG_FIELD_1_START : NG_FIELD_2_START) + s->order[i];
		if(j < line) j += s->vid->conf.lines;
		j -= line;
		
		if(j < 0 || j >= NG_DELAY_LINES)
		{
			/* We should never get to this point */
			fprintf(stderr, "*** Nagravision Syster scrambler is trying to read an invalid line ***\n");
			j = 0;
		}
	}
	
	vid_adj_delay(s->vid, NG_DELAY_LINES);
	
	/* Swap the active line with the oldest line in the delay buffer,
	 * with active video offset in j if necessary. */
	if(j > 0)
	{
		int16_t *dline = s->vid->oline[s->vid->odelay + j];
		
		/* For PAL the colour burst is not moved, just the active
		 * video. For SECAM the entire line is moved. */
		x = s->vid->active_left * 2;
		
		if(s->vid->conf.colour_mode == VID_SECAM) x = 0;
		
		for(; x < s->vid->width * 2; x += 2)
		{
			s->vid->output[x] = dline[x];
		}
	}
	
	_render_ng_vbi(s,line,0x72);
}
