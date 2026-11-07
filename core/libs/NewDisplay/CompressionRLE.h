#ifndef LIBS_NEWDISPLAY_COMPRESSIONRLE_H_
#define LIBS_NEWDISPLAY_COMPRESSIONRLE_H_

#include "stdint.h"

// plan 027: the compressed fonts / images are byte streams (was an array of value/count pairs, 12 KB more flash).
// Header byte h: h >= 0x80: (h & 0x7F) + 1 literal bytes follow; h < 0x80: the next byte is repeated h + 1 times.
typedef uint8_t CompressedData;

void decompress(const CompressedData* compressed, int compressed_size, uint8_t* decompressed);

#endif /* LIBS_NEWDISPLAY_COMPRESSIONRLE_H_ */
