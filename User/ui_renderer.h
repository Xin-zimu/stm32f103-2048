#ifndef __UI_RENDERER_H
#define __UI_RENDERER_H

#include "ui_page.h"
#include "ui_types.h"

typedef struct
{
    uint32_t task_calls;               // Times UI_RendererTask was entered.
    uint32_t busy_returns;             // Times renderer returned because work was still blocked.
    uint32_t dma_busy_returns;         // Times LCD DMA was still sending a strip.
    uint32_t no_buffer_returns;        // Times no strip buffer was free.
    uint32_t submit_busy_returns;      // Times LCD DMA rejected a prepared strip.
    uint32_t dirty_rects_started;      // Dirty rectangles consumed from the queue.
    uint32_t strips_drawn;             // Strip buffers rendered by CPU.
    uint32_t strips_submitted;         // Strip buffers accepted by LCD DMA.
    uint32_t bytes_submitted;          // RGB565 bytes accepted by LCD DMA.
} UI_RendererStats;

void UI_RendererInit(void);
void UI_RendererTask(uint32_t now, const UI_PageOps *page);
uint8_t UI_RendererIsBusy(void);
void UI_RendererRequestFull(void);
void UI_RendererGetStats(UI_RendererStats *stats);
void UI_RendererResetStats(void);

#endif
