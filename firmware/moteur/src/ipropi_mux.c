#include "ipropi_mux.h"

#include "can_protocol.h"
#include "hardware/gpio.h"

// Correspondance canal logique -> entrée du 74HC4067. Extraite telle quelle de
// l'auto-test (ADR-0001) : une seule copie dans le firmware.
static const uint8_t g_channel_to_mux_input[IPROPI_MUX_CHANNEL_COUNT] = {
    9u, 10u, 11u, 12u, 13u, 14u, 1u, 2u, 5u, 3u, 4u, 6u,
};

_Static_assert((sizeof(g_channel_to_mux_input) / sizeof(g_channel_to_mux_input[0])) ==
                   IPROPI_MUX_CHANNEL_COUNT,
               "La carte du mux doit couvrir tous les canaux Moteur");
_Static_assert(MOTEUR_CHANNEL_COUNT == CAN_IPROPI_CHANNEL_COUNT,
               "Le nombre de canaux Moteur doit correspondre au protocole");

void ipropi_mux_init(void) {
    gpio_init(MOTEUR_MUX_EN_GPIO);
    gpio_put(MOTEUR_MUX_EN_GPIO, true);  // mux déconnecté au repos
    gpio_set_dir(MOTEUR_MUX_EN_GPIO, GPIO_OUT);

    gpio_init(MOTEUR_MUX_S0_GPIO);
    gpio_set_dir(MOTEUR_MUX_S0_GPIO, GPIO_OUT);
    gpio_init(MOTEUR_MUX_S1_GPIO);
    gpio_set_dir(MOTEUR_MUX_S1_GPIO, GPIO_OUT);
    gpio_init(MOTEUR_MUX_S2_GPIO);
    gpio_set_dir(MOTEUR_MUX_S2_GPIO, GPIO_OUT);
    gpio_init(MOTEUR_MUX_S3_GPIO);
    gpio_set_dir(MOTEUR_MUX_S3_GPIO, GPIO_OUT);

    (void)ipropi_mux_select(0u);
}

bool ipropi_mux_select(uint8_t channel) {
    if (channel >= IPROPI_MUX_CHANNEL_COUNT) {
        return false;
    }
    const uint8_t mux_input = g_channel_to_mux_input[channel];

    gpio_put(MOTEUR_MUX_S0_GPIO, (mux_input & 0x01u) != 0u);
    gpio_put(MOTEUR_MUX_S1_GPIO, (mux_input & 0x02u) != 0u);
    gpio_put(MOTEUR_MUX_S2_GPIO, (mux_input & 0x04u) != 0u);
    gpio_put(MOTEUR_MUX_S3_GPIO, (mux_input & 0x08u) != 0u);
    return true;
}

void ipropi_mux_enable(void) {
    gpio_put(MOTEUR_MUX_EN_GPIO, false);
}

void ipropi_mux_disable(void) {
    gpio_put(MOTEUR_MUX_EN_GPIO, true);
}
