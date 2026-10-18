// GPIO du canal 0 et des signaux partagés, recopiés depuis la table IMP-001 de
// Docs/adr/adr-0001-hal-solenoide-drv8874-phen.md (contrat HAL verrouillé).
// Ne PAS dériver ce fichier du pinmap généré (Netlist/netlist_to_pinmap.py) :
// les noms de net générés reprennent "SENS", explicitement rejeté par l'ADR-0001
// pour ne pas laisser croire à une mesure de courant par canal.
// Périmètre Phase 2a : un seul canal (canal 0), pas de CAN, pas de multi-canal (2b).
// Machine à états pull/hold/release dans solenoid_driver.c (constantes dans
// solenoid_profile.h) et feature clock-test (clock_test.c) incluses.
#ifndef MOTEUR_CONFIG_H
#define MOTEUR_CONFIG_H

// Rôle logique : pin1 EN/IN1 du DRV8874, sortie PWM matérielle (hardware_pwm).
#define MOTEUR_PWM_CHANNEL0_GPIO 35u

// Rôle logique : pin2 PH/IN2 du DRV8874, sortie GPIO simple (direction).
#define MOTEUR_DIR_CHANNEL0_GPIO 0u

// Rôle logique : pin4 NFAULT du DRV8874, entrée GPIO actif bas.
#define MOTEUR_FAULT_CHANNEL0_GPIO 39u

// Rôle logique : pin3 NSLEEP, net partagé entre les 12 drivers.
#define MOTEUR_GLOBAL_SLEEP_GPIO 15u

// Bouton de test, actif bas.
#define MOTEUR_TEST_BUTTON_GPIO 34u

// Net /FREQ OSC (GPIO41, pinmap.generated_drive.yaml) : sortie du clock-test,
// diagnostic uniquement (feature clock-test-mode1). Hors HAL solénoïde.
#define MOTEUR_CLOCK_TEST_GPIO 41u

// Fréquence cible du clock-test : 1 MHz (clk_sys/150 pour clk_sys = 150 MHz).
#define MOTEUR_CLOCK_TEST_FREQ_HZ 1000000u

// --- CAN (ADR-0002, ADR-0003) : spécifique à la carte Moteur -------------------
// Bloc PIO et GPIO imposés par le câblage (/RXD = GPIO46, /TXD = GPIO47,
// Docs/specs/CAN.md). Constantes du protocole (IDs, débit...) : shared/protocol.
#define MOTEUR_CAN_PIO_NUM 0u
#define MOTEUR_CAN_GPIO_RX 46u
#define MOTEUR_CAN_GPIO_TX 47u
// Priorité IRQ la plus haute côté application (ADR-0002).
#define MOTEUR_CAN_IRQ_PRIORITY 0u

// Version du firmware, annoncée dans la trame B (0..127 chacun).
#define MOTEUR_FW_MAJOR 0u
#define MOTEUR_FW_MINOR 1u
#define MOTEUR_FW_PATCH 0u

#endif  // MOTEUR_CONFIG_H
