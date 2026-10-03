// GPIO du canal 0 et des signaux partagés, recopiés depuis la table IMP-001 de
// Docs/adr/adr-0001-hal-solenoide-drv8874-phen.md (contrat HAL verrouillé).
// Ne PAS dériver ce fichier du pinmap généré (Netlist/netlist_to_pinmap.py) :
// les noms de net générés reprennent "SENS", explicitement rejeté par l'ADR-0001
// pour ne pas laisser croire à une mesure de courant par canal.
// Périmètre Phase 2a uniquement : un seul canal, pas de CAN, pas de machine à états.
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

#endif  // MOTEUR_CONFIG_H
