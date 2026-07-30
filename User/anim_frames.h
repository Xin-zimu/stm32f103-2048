#ifndef __ANIM_FRAMES_H
#define __ANIM_FRAMES_H

#include "stm32f10x.h"

#define ANIM_FRAME_WIDTH          120U    // Indexed source width
#define ANIM_FRAME_HEIGHT         120U    // Indexed source height
#define ANIM_PIXEL_SCALE          2U      // Output scale to 240x240
#define ANIM_FRAME_COUNT          28U     // Number of retained GIF frames
#define ANIM_FRAME_INTERVAL_MS    40U     // Display time per retained frame
#define ANIM_FIRST_FRAME_SIZE     7200U   // Packed first-frame bytes
#define ANIM_DELTA_DATA_SIZE      49109U  // Encoded bytes for all transitions
#define ANIM_SOURCE_SHA256        "0f7b48e8a23a87624f2e50a9dd85632beebfbf3efd432f0c90620c2d25a20f87"

extern const uint16_t anim_palette[16];
extern const uint8_t anim_first_frame[ANIM_FIRST_FRAME_SIZE];
extern const uint32_t anim_delta_offsets[ANIM_FRAME_COUNT + 1U];
extern const uint8_t anim_delta_data[ANIM_DELTA_DATA_SIZE];

#endif
