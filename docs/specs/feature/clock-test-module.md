# feature/clock-test-mode1. 

## Objectif

1. Implémenter un mode de diagnostic matériel permettant de mesurer la fréquence réelle de l'horloge système (clk_sys) du RP2354B sur le point de test GPIO41 [VÉRIFIER DANS LES FICHIERS "pinmap.generated"]
2. Spécifications techniques & Contraintes matérielles
   
    Sortie de fréquence (Sortie) : GPIO41. Le GPIO41 n'étant pas une broche GPOUT matérielle directe du multiplexeur d'horloge, la fréquence sera générée via le périphérique PWM (ou un micro-programme PIO).Division d'horloge : Pour éviter d'émettre des parasites haute fréquence (EMI), le signal sur GPIO41 sera une sous-division /10 de clk_sys.Signal électrique : Activer le Slew Rate lent (SLEWFAST = 0) et régler le Drive Strength à 2 mA [9].3. Machine à états du Firmware (State Machine)[ État IDLE ] 
     │
     ▼ (Appui maintenu 5,0 secondes sur GPIO34)
[ État CLK_TEST_ACTIVE ] 
     │  ├── GPIO41 génère le carré X MHz (divisé depuis clk_sys)
     │      │
     ▼ (N'importe quel appui sur GPIO34)
[ État IDLE ]
        └── GPIO41 repasse en entrée / High-Z (Désactivé)

💡 Consignes d'implémentation C/C++ (Pico SDK) .


Configuration du PWM pour générer 1 MHz à partir de clk_sys (150 MHz)
gpio_set_function(41, GPIO_FUNC_PWM);
uint slice_num = pwm_gpio_to_slice_num(41);
uint chan = pwm_gpio_to_channel(41);

// Configuration pour diviser par 150 (Wrap = 149) -> Fréquence de sortie = 1 MHz
pwm_set_wrap(slice_num, 149);
pwm_set_chan_level(slice_num, chan, 75); // Rapport cyclique 50%

// Réglage pad pour limiter le bruit EMI
gpio_set_slew_rate(41, GPIO_SLEW_RATE_SLOW);
gpio_set_drive_strength(41, GPIO_DRIVE_STRENGTH_2MA);

pwm_set_enabled(slice_num, true);
Arrêt du signal (Mode IDLE) :// Désactivation du signal
uint slice_num = pwm_gpio_to_slice_num(41);
pwm_set_enabled(slice_num, false);
gpio_set_function(41, GPIO_FUNC_SIO);
gpio_set_dir(41, GPIO_IN); // Remet la broche en haute impédance
