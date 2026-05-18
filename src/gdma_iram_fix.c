#include "esp_attr.h"
#include <stdint.h>

/*
 * Correctif IDF 5.3.2 : gdma_hal_reset/append/start_with_desc/stop manquent
 * IRAM_ATTR mais sont appelés depuis l'ISR VSYNC (lcd_rgb_panel_try_restart_
 * transmission, IRAM_ATTR). Quand une écriture NVS désactive le cache via IPC,
 * l'ISR se déclenche et tente d'exécuter ces fonctions depuis la flash → panic.
 *
 * Les wrappers --wrap ci-dessous vivent en IRAM et écrivent directement les
 * registres DMA, sans passer par les pointeurs de fonction hal->reset/append
 * (eux-mêmes en flash). Offsets vérifiés sur le désassemblage de libhal.a :
 *
 *   stride canal : 0xC0
 *   in.conf0  (in_rst  b0)  : +0x00   out.conf0 (out_rst b0)  : +0x60
 *   in.link   (stop b21, start b22, restart b23, addr b19:0) : +0x20
 *   out.link  (stop b20, start b21, restart b22, addr b19:0) : +0x80
 */

typedef enum { GDMA_CHANNEL_DIRECTION_TX = 0, GDMA_CHANNEL_DIRECTION_RX = 1 } gdma_channel_direction_t;
typedef struct { void *dev; } gdma_hal_context_t;

#define CHAN(dev, id, off) \
    ((volatile uint32_t *)((uint8_t *)(dev) + (uint32_t)(id) * 0xC0u + (off)))

IRAM_ATTR void __wrap_gdma_hal_reset(gdma_hal_context_t *hal, int id, gdma_channel_direction_t dir)
{
    volatile uint32_t *r = (dir == GDMA_CHANNEL_DIRECTION_RX) ? CHAN(hal->dev, id, 0x00)
                                                               : CHAN(hal->dev, id, 0x60);
    uint32_t v = *r; *r = v | 1u;    /* rst = 1 */
    v = *r;          *r = v & ~1u;   /* rst = 0 */
}

IRAM_ATTR void __wrap_gdma_hal_append(gdma_hal_context_t *hal, int id, gdma_channel_direction_t dir)
{
    if (dir == GDMA_CHANNEL_DIRECTION_RX)
        *CHAN(hal->dev, id, 0x20) |= (1u << 23);   /* in.link.restart */
    else
        *CHAN(hal->dev, id, 0x80) |= (1u << 22);   /* out.link.restart */
}

IRAM_ATTR void __wrap_gdma_hal_start_with_desc(gdma_hal_context_t *hal, int id,
                                               gdma_channel_direction_t dir, intptr_t desc)
{
    uint32_t addr20 = (uint32_t)desc & 0xFFFFFu;
    volatile uint32_t *r = (dir == GDMA_CHANNEL_DIRECTION_RX) ? CHAN(hal->dev, id, 0x20)
                                                               : CHAN(hal->dev, id, 0x80);
    uint32_t v = ((uint32_t)*r >> 20) << 20;  /* bits[31:20] conservés */
    *r = v | addr20;
    *r = v | addr20 | (1u << (dir == GDMA_CHANNEL_DIRECTION_RX ? 22 : 21));  /* start */
}

IRAM_ATTR void __wrap_gdma_hal_stop(gdma_hal_context_t *hal, int id, gdma_channel_direction_t dir)
{
    if (dir == GDMA_CHANNEL_DIRECTION_RX)
        *CHAN(hal->dev, id, 0x20) |= (1u << 21);   /* in.link.stop */
    else
        *CHAN(hal->dev, id, 0x80) |= (1u << 20);   /* out.link.stop */
}
