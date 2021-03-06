﻿// This file provides a documented decryptor of Riot Games's packman (stub) packer.
// This method works as of 20th July 2018 on League of Legends patch 8.14.
//
//

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// GKey holds the previous and saves for the future the cipher state for each decryption event.
// It is seeded with an initial value in SpawnKey() and saved after each Decrypt() call as each
// prevous rotation of the cipher is required for the next Decrypt() in the decryption chain.
// 
// Both count and hold have unsigned values from 0-255, as they represent indexes on the key, they
// are intended to overflow.
struct GKey
{
	uint8_t key[0x100];
	uint8_t count;
	uint8_t hold;
};

// Part of Microsoft PE struct
// documented here https://msdn.microsoft.com/en-au/library/ms809762.aspx
// & https://docs.microsoft.com/en-us/windows/desktop/debug/pe-format
// and in WINNT.h
struct IMAGE_IMPORT_DESCRIPTOR
{
	uint32_t import_lookup_table_rva;
	uint32_t timestamp;
	uint32_t forwarder_chain;
	uint32_t name_rva;
	uint32_t import_address_table_rva;
};
struct IMAGE_IMPORT_BY_NAME
{
	uint16_t hint;
	uint8_t name[1];
};
struct IMAGE_DATA_DIRECTORY {
	uint32_t va;
	uint32_t size;
};

void ReadFile(
	const char* in,
	const char* mode,
	uint8_t** out,
	int* len);

void WriteFile(
	const char* name,
	const void* data,
	size_t len);

void Decrypt(
	GKey* gk,
	const void* in,
	void* out,
	size_t len);

void SpawnKey(
	GKey* gk,
	const uint8_t* seed,
	size_t len);

void ReadFile(
	const char* in,
	const char* mode,
	uint8_t** out,
	int* len)
{
	FILE* f = fopen(in, mode);

	if (f == NULL || fseek(f, 0, SEEK_END))
		return;

	*len = ftell(f);

	if (*len == -1) {
		fclose(f);
		return;
	}

	*out = (uint8_t*)malloc(*len);

	fseek(f, 0, SEEK_SET);
	if (fread(*out, 1, *len, f) != *len) {
		fclose(f);
		return;
	}

	fclose(f);
	return;
}

void WriteFile(
	const char * name,
	const void * data,
	size_t len)
{
	FILE* f = fopen(name, "wb");

	if (f == NULL) {
		printf("Error writing file\n");
		return;
	}

	size_t r = fwrite(data, 1, len, f);

	if (r != len)
		printf("Error writing file\n");

	fclose(f);
	return;
}

// Decrypts the {len} size byte array at {in} and writes it to {out},
// advances {gk} with every byte decrypted
void Decrypt(
	GKey* gk,
	const void* in,
	void* out,
	size_t len)
{
	uint8_t t1, t2;
	uint8_t j;

	// Each byte is decrypted one by one
	for (uint32_t i = 0; i < len; i++) {

		// Persisted count from GKey is incremented first
		gk->count++;
		j = gk->count;

		// Persisted carry (hold) value is added to from the value at key[count]
		gk->hold += gk->key[j];

		// The values at key[count] and key[hold] are swapped with eachother
		t1 = gk->key[j];
		t2 = gk->key[gk->hold];
		gk->key[j] = t2;
		gk->key[gk->hold] = t1;

		// The value of key[count] is added to key[hold]. Unsigned overflow is intended
		t1 += t2;

		// in[i] represents the current byte being decrypted
		// it is xored with the the value at key[t1]
		// t1 being the result of adding key[hold] to key[count]
		// the result is stored in out[i] which may or may not be the same address of in[i]
		((uint8_t*)out)[i] = ((uint8_t*)in)[i] ^ gk->key[t1];
	}
}

// Spawns the GKey to be used to in Decrypt() from an initial seed value
void SpawnKey(
	GKey* gk,
	const uint8_t* seed,
	size_t len)
{
	// Initial state for key is successively incrementing bytes from 0-FF
	for (int i = 0; i < 0x100; i++) {
		gk->key[i] = i;
	}

	// The key is jumbled by the seed given
	// key[i] is added to seed[i]. i % len keeps the index inside the seed boundary
	// result is placed into h, which may be non-zero from the previous loop
	// key[i] and key[h] are swapped and the loop is continued
	uint8_t h = 0;
	for (int i = 0; i < 0x100; i++) {
		uint8_t j;
		j = gk->key[i];
		h += seed[i % len] + j;
		gk->key[i] = gk->key[h];
		gk->key[h] = j;
	}
}

int main() {

	// Read stub and league binaries into memory

	int len = 0, slen = 0;
	uint8_t* league;
	uint8_t* stub;

	ReadFile("League of Legends.exe", "rb", &league, &len);
	ReadFile("stub.dll", "rb", &stub, &slen);

	if (!slen || !len) {
		printf("We need both \"League of Legends.exe\" and \"stub.dll\" for this to work\n");
		return 0;
	}

	// -------
	// Stage 1 - Unpacking initial .text bytes into the intermediate 1st decrypted state

	// Keep in mind these are offsets on the file, not RVAs

	// We need a pointer to the .text section in League of Legends.exe
	uint8_t* ltext = league + 0x1000;

	// Length is not obfuscated in the PE header and can be put read from there instead of being
	// static here
	size_t ltext_len = 0x10BF000;

	// Pointer and length of the seed for the first GKey decryption chain
	uint8_t* decrypt1_seed = stub + 0x131590;
	size_t decrypt1_seed_len = 0x61;

	// Pointer and length of data that is decrypted outside of the .text section
	// I don't know what it's for but we must maintain the same decryption chain as stub does 
	// else our key will decrypt garbage
	uint8_t* decrypt1_data = league + 0x17CE040;
	size_t decrypt1_data_len = 0x4;

	// This is the seed for the second and final stage of .text decryption
	uint8_t* decrypt2_seed = stub + 0x12E660;
	size_t decrypt2_seed_len = 0x79;

	// Declare a GKey gk, zero it and spawn our key with the seed
	GKey gk;
	memset(&gk, 0, sizeof(GKey));
	SpawnKey(&gk, decrypt1_seed, decrypt1_seed_len);

	// Decrypt the 4 unknown bytes
	uint8_t something_important[4];
	Decrypt(&gk, decrypt1_data, something_important, decrypt1_data_len);

	// Decrypt the entire .text section
	Decrypt(&gk, ltext, ltext, ltext_len);

	// -------
	// Stage 2 - Import decryption

	// Pointers to the 'real' Import Table the one pointed to by the PE header is garbage
	// and to an array of name lengths stored in stub.dll
	IMAGE_IMPORT_DESCRIPTOR* import_descriptor_ptr = (IMAGE_IMPORT_DESCRIPTOR*)(league + 0x13D4B10);
	uint32_t* import_name_len_ptr = (uint32_t*)(stub + 0xBF5C8);

	// For later to fix PE header
	size_t iat_len = 0;

	// There are 19 imports
	for (int i = 0; i < 0x13; i++) {

		// Read the first import descriptor in the import descriptor table.
		// 0x14 is the size of each struct
		Decrypt(&gk, import_descriptor_ptr, import_descriptor_ptr, 0x14);

		// stub.dll has an array of name lengths; get the first one
		size_t len = *import_name_len_ptr;

		// decrypt the pointer to the  
		uint8_t* name_ptr = league + import_descriptor_ptr->name_rva;
		Decrypt(&gk, name_ptr, name_ptr, len);

		// printf("%s\n", name_ptr);

		// name_ptr is now a null terminated string containing the path of the import
		// (eg. BugSplat.dll)

		// LoadLibrary is called in stub here

		// get pointer to the IAT and ILT
		uint32_t* iat_ptr = (uint32_t*)(league + import_descriptor_ptr->import_address_table_rva);
		uint32_t* ilt = (uint32_t*)(league + import_descriptor_ptr->import_lookup_table_rva);

		// Walk the import lookup table until we hit NULL
		uint32_t hintarray_rva;
		do {
			// decrypt the address of the lookup table 
			Decrypt(&gk, ilt, ilt, 0x4);

			// deref the decrypted ILT entry
			// this is an rva to a hint/name struct
			hintarray_rva = *(uint32_t*)ilt;

			// IMAGE_ORDINAL_FLAG32 is the most signicant bit (0x80000000)
			// if this is set there is no function name to decrypt

			if (hintarray_rva && !(hintarray_rva & 0x80000000)) {
				// get our real pointer
				IMAGE_IMPORT_BY_NAME* hint_ptr = (IMAGE_IMPORT_BY_NAME*)(league + hintarray_rva);

				// increment the stub name length array ptr
				import_name_len_ptr++;

				// +2 because I guess they didn't add in the extra size of the struct? 
				// I'm not sure why they did this
				len = *(import_name_len_ptr)+0x2;

				// decrypt the IMAGE_IMPORT_BY_NAME at hint_ptr
				Decrypt(&gk, hint_ptr, hint_ptr, len);

				// GetProcAddress is called with hint_ptr->name here
				// printf("    %s\n", hint_ptr->name);
			}

			// Decrypt the IAT entry, it should be 0
			Decrypt(&gk, iat_ptr, iat_ptr, 0x4);

			// Put rva into IAT
			*iat_ptr = hintarray_rva;

			// Increment the pointers for next loop
			iat_ptr++;
			ilt++;
			iat_len += 0x4;
			// end when we hit NULL 
		} while (hintarray_rva);

		// go to the next import_descriptor
		import_descriptor_ptr++;
		// increment the pointer for the next import name len
		import_name_len_ptr++;

	} // end of IAT loading loop

	// Reconstruct the exe image

	// 0x3C offset to the PE header 
	uint32_t* pe_header_loc = (uint32_t*)(league + 0x3C);
	uint8_t* pe = league + *pe_header_loc;

	uint32_t* addressofentrypoint = (uint32_t*)(pe + 0x28);
	*addressofentrypoint = 0x102A692;

	// 0x78 is the offset to the IMAGE_DATA_DIRECTORY array
	pe += 0x78;

	// 1 and 12 are the indexes of ENTRY_IMPORT and ENTRY_IAT in the IMAGE_DATA_DIRECTORY array
	IMAGE_DATA_DIRECTORY* idt =
		(IMAGE_DATA_DIRECTORY*)(pe + (sizeof(IMAGE_DATA_DIRECTORY) * 1));
	IMAGE_DATA_DIRECTORY* iat =
		(IMAGE_DATA_DIRECTORY*)(pe + (sizeof(IMAGE_DATA_DIRECTORY) * 12));

	// Size remains OK
	idt->va = 0x13D4B10;

	// The VA of the IAT is at the top of .rdata
	iat->va = 0x10C0000;
	iat->size = iat_len;

	// give execute and read permission to .text
	// PE + 0x78 + 0xA4 is the permissions of the first section which should be from .text
	uint32_t* text_characteristics = (uint32_t*)(pe + 0xA4);
	*text_characteristics = 0x60000020;

	// -------
	// Stage 3 - .text second decryption

	// .text pages are all encrypted separately to allow non-sequential decryption
	// 4096 byte page size
	uint32_t num_pages = ltext_len / 0x1000;

	// loop for each page, starting at 1
	for (uint32_t i = 1; i <= num_pages; i++) {

		// zero out or create a new GKey for each page
		memset(&gk, 0, sizeof(GKey));

		// the decrypt2 seed is 0x79 in length but there are 0x53 of them
		// the modulus of the page number against 0x53 is whichever one is used
		uint8_t* seed = decrypt2_seed + ((i % 0x53) * decrypt2_seed_len);

		// pointer to our specific page
		uint8_t* text = league + (i * 0x1000);

		// create a key for this new GKey to use on this page
		SpawnKey(&gk, seed, decrypt2_seed_len);

		// decrypt the page in place
		Decrypt(&gk, text, text, 0x1000);
	}

	// -------
	// Stage 4 - .reloc

	// the relocation section has been moved from .reloc but not encrypted
	// it is used after stage 2 decryption of each page in .text to relocate addresses

	IMAGE_DATA_DIRECTORY* reloc =
		(IMAGE_DATA_DIRECTORY*)(pe + (sizeof(IMAGE_DATA_DIRECTORY) * 5));

	// file offset to .reloc
	uint8_t* reloc_ptr = league + 0x1721000;
	size_t reloc_size = reloc->size;

	// location of moved .reloc
	uint8_t* stub_ptr = league + 0x17CE000;
	stub_ptr += 0xB3A;

	// copy into proper reloc
	memcpy(reloc_ptr, stub_ptr, reloc_size);
	// an alternative would be to move the reloc VA

	WriteFile("League of Legends_unpacked.exe", league, len);

	// end
	free(league);
	free(stub);
	return 0;
}