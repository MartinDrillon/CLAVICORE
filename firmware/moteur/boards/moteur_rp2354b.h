/*
 * Board header custom pour la carte Moteur (Solenoide_drive, IC1 = RP2354B QFN80).
 *
 * INCERTITUDE TECHNIQUE NON RÉSOLUE (à valider par l'Architecte avant flash réel) :
 * Pico-SDK 2.3.1 (C:/Users/marti/.pico-sdk/sdk/2.3.1) ne contient AUCUNE référence
 * à "RP2354" (grep vide sur tout le SDK) : il n'existe pas de board officiel pour ce
 * chip dans cette version du SDK. Le RP2354B est annoncé par Raspberry Pi comme
 * pin-compatible avec le RP2350B (même die, QFN80, 48 GPIO) avec 2 Mo de flash
 * ajoutés dans le boîtier. Ce header approxime donc le RP2354B en réutilisant la
 * configuration RP2350B (variante "B", cf. weact_studio_rp2350b_core.h comme
 * modèle), avec deux points non vérifiés dans ce repo :
 *   1. PICO_FLASH_SIZE_BYTES ci-dessous (2 Mo) est une valeur déduite des annonces
 *      publiques du RP2354, PAS confirmée par une datasheet présente dans ce dépôt.
 *   2. Le stage2 de boot (PICO_BOOT_STAGE2_CHOOSE_W25Q080) est repris tel quel du
 *      Pico 2 ; la flash embarquée dans le boîtier RP2354 n'est pas garantie
 *      utiliser le même jeu de commandes SPI qu'un Winbond W25Q080 externe.
 * Ne pas lever ces deux incertitudes silencieusement : elles doivent être
 * confirmées (datasheet RP2354 ou test réel sur prototype) avant tout flash.
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_MOTEUR_RP2354B_H
#define _BOARDS_MOTEUR_RP2354B_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

// For board detection
#define MOTEUR_RP2354B

// --- RP2350 VARIANT ---
// RP2354B = variante "B" (QFN80, 48 GPIO), comme RP2350B.
#define PICO_RP2350A 0

// --- UART --- (non câblé/utilisé par ce firmware de test isolé, valeurs par défaut du SDK)
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// --- FLASH ---
// Cf. bloc d'incertitude en tête de fichier : valeur ET stage2 non vérifiés.
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
