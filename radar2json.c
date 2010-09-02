/*
 *  radar2json.c - converts binary radar product files from noaa.gov into JSON
 *  
 *  version 10.08.30
 *  
 *  Copyright (c) 2010 Justin Ouellette
 *  
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *  
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>

static inline short halfword(const unsigned char *s, const unsigned int i) {
	return (s[i] << 8) | s[i+1];
}

static inline int word(const unsigned char *s, const unsigned int i) {
	return (s[i] << 24) | (s[i+1] << 16) | (s[i+2] << 8) | s[i+3];
}

int main (const int argc, const char * argv[]) {
	if (argc != 2) {
		fprintf(stderr,"Usage: radar2json <file>\n");
		return 1;
	}

	FILE *f = fopen(argv[1],"rb");
	if (f == NULL) {
		fprintf(stderr,"Error opening file: %s\n",argv[1]);
		return 2;
	}

	fseek(f,0,SEEK_END);
	long size = ftell(f);
	rewind(f);

	unsigned char *b = (unsigned char *)malloc(sizeof(unsigned char) * size);
	if (b == NULL) {
		fprintf(stderr,"Error allocating memory.\n");
		return 3;
	}

	size_t result = fread(b,1,size,f);
	if (result != size) {
		fprintf(stderr,"Error reading file.\n");
		return 4;
	}
	
	fclose(f);
	printf("{");

	int i;
	unsigned char wmo[18];
	for (i = 0; i < 18; i++)
		wmo[i] = b[i];
	printf("\"wmo_header\":\"%s\"",wmo);

	unsigned char awips[6];
	for (i = 21; i < 27; i++)
		awips[i - 21] = b[i];
	printf(",\"awips_id\":\"%s\"",awips);
	
	printf(",\"message_code\":%d",halfword(b,30));
	
	int t = ((halfword(b,32) - 1) * 86400) + word(b,34);
	printf(",\"message_time\":%d",t);

	if (word(b,38) != size - 30) {
		fprintf(stderr,"Error verifying file length.\n");
		return 5;
	}
	
	printf(",\"source_id\":%d",halfword(b,42));
	
	printf(",\"destination_id\":%d",halfword(b,44));
	
	if (halfword(b,48) != -1) {
		fprintf(stderr,"Error finding product description block.\n");
		return 6;
	}
	
	printf(",\"sequence_number\":%d",halfword(b,66));	
	printf(",\"radar_latitude\":%.3f",word(b,50) / 1000.0f);
	printf(",\"radar_longitude\":%.3f",word(b,54) / 1000.0f);
	printf(",\"radar_altitude\":%d",halfword(b,58));
	printf(",\"product_code\":%d",halfword(b,60));
	printf(",\"operational_mode\":%d",halfword(b,62));	
	printf(",\"volume_coverage_pattern\":%d",halfword(b,64));
	printf(",\"volume_scan_number\":%d",halfword(b,68));
	
	t = ((halfword(b,70) - 1) * 86400) + word(b,72);
	printf(",\"volume_scan_time\":%d",t);	
	
	t = ((halfword(b,76) - 1) * 86400) + word(b,78);
	printf(",\"product_generation_time\":%d",t);
	
	printf(",\"elevation_number\":%d",halfword(b,86));
	printf(",\"elevation_angle\":%.1f",halfword(b,88) / 10.0f);
	
	printf(",\"maximum_reflectivity\":");
	short m = halfword(b,122);
	if (m == -33) {
		printf("null");
	} else {
		printf("%d",m);
	}
	
	printf(",\"calibration_constant\":%d",b[130]);
	
	for (i = 1; i <= 16; i++) {
		printf(",\"threshold_%d\":\"",i);
		
		unsigned char msb = b[90 + 2*(i - 1)];
		unsigned char lsb = b[91 + 2*(i - 1)];
		
		if (msb & 1)
			printf("-");
		if (msb & 2)
			printf("+");
		if (msb & 4)
			printf("<");
		if (msb & 8)
			printf(">");
		
		if (msb & 128) {
			if (lsb == 1)
				printf("TH");
			if (lsb == 2)
				printf("ND");
			if (lsb == 3)
				printf("RF");
		} else {
			if (msb & 16)
				printf("%.1f",lsb / 10.0f);
			if (msb & 32)
				printf("%.2f",lsb / 20.0f);
			if (msb & 64)
				printf("%.2f",lsb / 100.0f);				
			if (!(msb & 16 || msb & 32 || msb & 64))
				printf("%d",lsb);
		}
		
		printf("\"");
	}
	
	unsigned int sym_offset = 30 + 2 * halfword(b,140);
	
	if(halfword(b,sym_offset) != -1 || halfword(b,sym_offset+2) != 1) {
		fprintf(stderr,"Error finding product symbology block.\n");
		return 7;
	}
	
	printf(",\"layers\":[");
	short layer_count = halfword(b,sym_offset+8);
	int previous_layer_length = 0;
	for (i = 0; i < layer_count; i++) {
		unsigned int o = sym_offset + 10 + previous_layer_length;
		
		if (halfword(b,o) != -1) {
			fprintf(stderr,"Error finding layer %d.\n",i);
			return 8;
		}
		
		printf(i ? ",{" : "{");
		
		if (b[o+6] != 175 || b[o+7] != 31) {
			fprintf(stderr,"Error, only radial data supported for now.\n");
			return 9;			
		}
		
		printf("\"index_of_first_range_bin\":%d",halfword(b,o+8));
		printf(",\"range_bin_count\":%d",halfword(b,o+10));
		printf(",\"i_center_of_sweep\":%d",halfword(b,o+12));
		printf(",\"j_center_of_sweep\":%d",halfword(b,o+14));
		printf(",\"scale_factor\":%.3f",halfword(b,o+16) / 1000.0f);
		
		short rc = halfword(b,o+18);
		printf(",\"radial_count\":%d",rc);
		
		if (rc)
			printf(",\"radials\":[");
		
		int j;
		unsigned int ro = o + 20;
		for (j = 0; j < rc; j++) {
			printf(j ? ",{" : "{");
			
			short rle_halfword_count = halfword(b,ro);
			printf("\"start_angle\":%.1f",halfword(b,ro+2) / 10.0f);
			printf(",\"angle_delta\":%.1f",halfword(b,ro+4) / 10.0f);
			printf(",\"range_bins\":[");
			ro = ro + 6;
			
			int k;
			for (k = 0; k < (rle_halfword_count * 2); k++) {
				unsigned char rle = b[ro];
				unsigned char length = rle >> 4;
			 	unsigned char value = rle & 15;
			
				int l;
				for (l = 0; l < length; l++) {
					if (!(k == 0 && l == 0))
						printf(",");					
					printf("%d",value);
				}

				ro++;
			}
			
			printf("]}");
		}
		
		printf("]}");
	}
	printf("]}");
	
	free(b);
	
    return 0;
}
