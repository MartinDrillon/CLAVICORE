// Constantes de la chaîne de mesure IPROPI (ADR-0009 §5c/§5d).
// Déplacées de l'ancien `solenoid_profile.h` (ADR-0013 §2) : elles
// caractérisent la CARTE (documentation DRV8874 + schéma) et n'interviennent
// pas dans l'activation. Elles ne font donc PAS partie du profil transmis par
// le Clavier. Aucune valeur n'a changé.
#ifndef MOTEUR_SOLENOID_OHM_CONSTANTS_H
#define MOTEUR_SOLENOID_OHM_CONSTANTS_H

#define SOLENOID_IPROPI_UA_PER_A 450u      // A_IPROPI du DRV8874 (450 µA/A)
#define SOLENOID_IPROPI_RESISTOR_OHM 2400u // R_IPROPI vers GND (2,4 kΩ)
#define SOLENOID_VM_MILLIVOLT 24000u       // alimentation 24 V (à mesurer)
#define SOLENOID_ADC_VREF_MILLIVOLT 3300u  // référence ADC (à mesurer)
#define SOLENOID_RDSON_MILLIOHM 240u       // R_DS(on) du DRV8874 (datasheet)

#endif  // MOTEUR_SOLENOID_OHM_CONSTANTS_H
