/*
 * This file is part of GRFCodec.
 * GRFCodec is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * GRFCodec is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/mman.h>

#include "version.h"

size_t _file_length;
uint8_t *_file_buffer;
uint8_t *_buffer;

inline void SkipBytes(size_t count)
{
	_buffer += count;
}

inline uint8_t ReadByte()
{
	return *(_buffer++);
}

inline uint16_t ReadWord()
{
	uint16_t v = ReadByte();
	return v | (ReadByte() << 8);
}

inline uint32_t ReadDWord()
{
	uint32_t v = ReadWord();
	return v | (ReadWord() << 16);
}

void SkipSpriteData(uint8_t type, uint16_t num)
{
	if (type & 2) {
		SkipBytes(num);
	} else {
		while (num > 0) {
			int8_t i = ReadByte();
			if (i >= 0) {
				int size = (i == 0) ? 0x80 : i;
				num -= size;
				SkipBytes(size);
			} else {
				i = -(i >> 3);
				num -= i;
				ReadByte();
			}
		}
	}
}

inline uint32_t Swap32(uint32_t x)
{
	return ((x >> 24) & 0xFF) | ((x >> 8) & 0xFF00) | ((x << 8) & 0xFF0000) | ((x << 24) & 0xFF000000);
}

const char *GetGrfID(const char *filename, uint32_t *grfid)
{
	*grfid = 0;

	FILE *f = fopen(filename, "rb");
	if (f == NULL) return "Unable to open file";

	/* Get the length of the file */
	fseek(f, 0, SEEK_END);
	_file_length = ftell(f);
	fseek(f, 0, SEEK_SET);

	/* Map the file into memory */
	_file_buffer = (uint8_t*)mmap(NULL, _file_length, PROT_READ, MAP_PRIVATE, fileno(f), 0);
	_buffer = _file_buffer;

	/* Check the magic header, or what there is of one */
	if (ReadWord() != 0x04 || ReadByte() != 0xFF) return "No magic header";

	/* Number of sprites. */
	ReadDWord();

	while (_buffer < _file_buffer + _file_length) {
		uint16_t num = ReadWord();
		if (num == 0) break;

		uint8_t type = ReadByte();
		if (type == 0xFF) {
			/* Pseudo sprite */
			uint8_t action = ReadByte();
			if (action == 0x08) {
				/* GRF Version, do not care */
				ReadByte();
				/* GRF ID */
				*grfid = Swap32(ReadDWord());
				/* No more, we don't care */
				break;
			} else {
				SkipBytes(num - 1);
			}
		} else {
			SkipBytes(7);
			/* Skip sprite data */
			SkipSpriteData(type, num - 8);
		}
	}

	munmap(_file_buffer, _file_length);
	fclose(f);

	return (*grfid == 0) ? "File valid but no GrfID found" : NULL;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		printf(
			"GRFID version " GRFCODECVER "\n"
			"\n"
			"Usage:\n"
			"    GRFID <NewGRF-File>\n"
			"        Get the GRF ID from the NewGRF file\n"
			"\n"
			"You may copy and redistribute it under the terms of the GNU General Public\n"
			"License, as stated in the file 'COPYING'.\n");
		return 1;
	}

	uint32_t grfid;
	const char *err = GetGrfID(argv[1], &grfid);

	if (err == NULL) {
		printf("%08x\n", grfid);
		return 0;
	}

	printf("Unable to get GrfID: %s\n", err);
	return 1;
}
