/* Client : Exploitation Bio Damien Bettex 
   Projet : Dynamiseur
   Date : juin 2019
   Développeur : BF
   Description : Programme de gestion d'un dynamiseur d'eau
*/

#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 20, 4); // PIN MEGA - SDA:20, SCL:21

// Pinout configuration

const int capteurForwardPin = 2;        // Pin du capteur inductif 1 (position initiale)
const int capteurBackwardPin = 3;       // Pin du capteur inductif pour compter les tours
const int valve1Pin = 4;                // Pin de l'électrovanne 1
const int valve2Pin = 5;                // Pin de l'électrovanne 2
const int alarmLedPin = 6;              // Pin de la LED d'alarme
const int inProgressLedPin = 7;         // Pin de la LED de programme en cours
const int startButtonPin = 8;           // Pin du bouton de démarrage
const int directionButtonPin = 9;       // Pin du bouton de sélection de la direction
const int cycleSwitchPin = 10;          // Pin du bouton pour sélectionner avec ou sans cycle de fin
const int programSelectButton1Pin = 11; // Pin du bouton pour le choix du programme 1
const int programSelectButton2Pin = 12; // Pin du bouton pour le choix du programme 2
const int incLitresButtonPin = 13;      // Pin pour augmenter les litres à ajouter
const int decLitresButtonPin = 14;      // Pin pour diminuer les litres à ajouter
const int incTurnsButtonPin = 15;       // Pin pour augmenter le nombre de tours
const int decTurnsButtonPin = 16;       // Pin pour diminuer le nombre de tours
const int emergencyStopPin = 18;        // Exemple de pin pour le bouton d'arrêt d'urgence
const int flowMeterPin = 19;            // Pin du débitmètre
const int motorDirLeftPin = 22;         // Pin du moteur
const int motorDirRightPin = 23;        // Pin du moteur
const int alarmResetPin = 24;           // Pin de reset après alarme

// Variables globales
volatile int flowPulseCount = 0;        // Compteur de pulses du débitmètre
volatile int tourCountForward = 0;      // Compteur de tours du moteur
volatile int tourCountBackward = 0;     // Compteur de tours du moteur
volatile bool emergencyStop = false;    // Variable globale pour détecter l'arrêt d'urgence

int litres = 1;                         // Nombre de litres à ajouter
int turns = 3;                          // Nombre de tours du moteur
int direction = 1;                      // Direction du moteur (1 = horaire, -1 = anti-horaire)
bool inProgress = false;                // Statut du programme en cours
bool cycleWithEnd = false;              // Si on utilise le cycle de fin ou non
bool readyForEnd = false;
int programSelected = 0;                // Programme sélectionné (1 = rotation et remplissage, 2 = seulement rotation, etc.)

unsigned long previousMillisValve = 0;  // Temps où la valve 2 s'est ouverte
unsigned long valve2StayOpen = 60000;   // Valve 2 reste ouverte 60 secondes
bool valve2Open = false;                // Statut de l'ouverture de la valve 2

unsigned long lastInterruptTime = 0;      // Dernière fois où l'interruption a eu lieu
const unsigned long debounceDelay = 500;  // Délai de dé-bounce en millisecondes

// Variables pour suivre l'état du bouton pour le direction du dépat
int directionButtonState = 0;
int previousDirectionButtonState = 0;


void setup() {

  Serial.begin(9600);
  // Configuration des pins
  pinMode(motorDirLeftPin, OUTPUT);
  pinMode(motorDirRightPin, OUTPUT);
  pinMode(valve1Pin, OUTPUT);
  pinMode(valve2Pin, OUTPUT);
  pinMode(alarmLedPin, OUTPUT);
  pinMode(inProgressLedPin, OUTPUT);
  pinMode(startButtonPin, INPUT_PULLUP);
  pinMode(directionButtonPin, INPUT_PULLUP);
  pinMode(cycleSwitchPin, INPUT_PULLUP);
  pinMode(programSelectButton1Pin, INPUT_PULLUP);
  pinMode(programSelectButton2Pin, INPUT_PULLUP);
  pinMode(incLitresButtonPin, INPUT_PULLUP);
  pinMode(decLitresButtonPin, INPUT_PULLUP);
  pinMode(incTurnsButtonPin, INPUT_PULLUP);
  pinMode(decTurnsButtonPin, INPUT_PULLUP);
  pinMode(capteurForwardPin, INPUT);        // Capteur inductif pour la position initiale
  pinMode(capteurBackwardPin, INPUT);       // Capteur inductif pour compter les tours
  pinMode(emergencyStopPin, INPUT_PULLUP);  // Arrêt d'urgence
  pinMode(alarmResetPin, INPUT_PULLUP);     // Arrêt d'urgence
  
  // Attacher l'interruption pour compter les tours
  attachInterrupt(digitalPinToInterrupt(emergencyStopPin), stopMotor, FALLING); // Configurer les ISR
  attachInterrupt(digitalPinToInterrupt(capteurForwardPin), countToursForwardISR, RISING);
  attachInterrupt(digitalPinToInterrupt(capteurBackwardPin), countToursBackwardISR, RISING);
  attachInterrupt(digitalPinToInterrupt(flowMeterPin), flowMeterISR, RISING);

  // Initialisation de l'écran LCD
  lcd.init();
  lcd.backlight();

  // Affichage d'un message de démarrage
  lcd.setCursor(0, 0);
  lcd.print("Dynamiseur");
  lcd.setCursor(0, 1);
  lcd.print("Initialisation");
  delay(2000);
  lcd.clear();

  Serial.println("Système prêt");
}

void loop() {
  
  // Reset après l'arrêt d'urgence
  if (digitalRead(alarmResetPin) == LOW) {
    emergencyStop = false; ;  // Programme de rotation et remplissage
    digitalWrite(alarmLedPin, LOW);
    Serial.println("Système prêt");
    delay(200); // Anti-rebond
  }
  
  // Sélection du programme
  if (digitalRead(programSelectButton1Pin) == LOW) {
    programSelected = 1;  // Programme de rotation et remplissage
    //Serial.println("Programme 1");
  }
  if (digitalRead(programSelectButton2Pin) == LOW) {
    programSelected = 2;  // Programme de rotation uniquement
    //Serial.println("Programme 2");
  }

  // Modification des paramètres de litres et tours
  if (digitalRead(incLitresButtonPin) == LOW) {
    litres++;
    delay(200); // Anti-rebond
  }
  if (digitalRead(decLitresButtonPin) == LOW) {
    litres--;
    delay(200); // Anti-rebond
  }
  if (digitalRead(incTurnsButtonPin) == LOW) {
    turns++;
    delay(200); // Anti-rebond
  }
  if (digitalRead(decTurnsButtonPin) == LOW) {
    turns--;
    delay(200); // Anti-rebond
  }

  // Variables pour suivre l'état du bouton pour le direction du dépat
  directionButtonState = digitalRead(directionButtonPin);  // Lire l'état du bouton

  // Vérifiez si l'état du bouton a changé
  if (directionButtonState != previousDirectionButtonState) {
    if (directionButtonState == HIGH) {
      // Le bouton est appuyé, alterner entre les deux positions
      direction = -direction;  // Change entre 1 et -1
      Serial.print("Position de départ : ");
      Serial.println(direction == 1 ? "Nord" : "Sud");
      lcd.clear();
    }
    delay(200);  // Anti-rebond pour éviter les multiples lectures
  }
  previousDirectionButtonState = directionButtonState;  // Mémoriser l'état précédent du bouton

/* 
  Pour afficher les caractère ascii --> https://docs.wokwi.com/parts/wokwi-lcd1602
  lcd.setCursor(0, 0);
  lcd.print("Syst");
  lcd.write(232);    
  lcd.print("me pr");
  lcd.write(234);
  lcd.print("t !");
*/

  // Affichage sur l'écran LCD
  lcd.setCursor(0, 0);
  lcd.print("Programme   : ");
  lcd.print(digitalRead(programSelectButton1Pin) == LOW  ? "1" : "2");
  lcd.setCursor(0, 1);
  lcd.print("Nbr. litres : ");
  lcd.print(litres);
  lcd.setCursor(0, 2);
  lcd.print("Nbr. tours  : ");
  lcd.print(turns);
  lcd.setCursor(0, 3);
  lcd.print("Direction   : ");
  lcd.print(direction == 1 ? "Nord" : "Sud");
 // lcd.setCursor(0, 3);
  //lcd.print(emergencyStop);

  // Détection du cycle de fin
  cycleWithEnd = (digitalRead(cycleSwitchPin) == LOW);

  // Démarrage du programme
  if (digitalRead(startButtonPin) == LOW && !inProgress && !emergencyStop) {
    Serial.println("Programme en cours");
    digitalWrite(inProgressLedPin, HIGH);
    inProgress = true;
    //moveToPosition();   // Positionner le moteur selon le capteur 1
    executeProgram();
  }

  // Gestion de la fermeture de la valve 2 après 60 secondes de non-blocage
  if (valve2Open && millis() - previousMillisValve >= valve2StayOpen) {
    digitalWrite(valve2Pin, LOW);   // Ferme la valve 2
    valve2Open = false;             // Réinitialise le statut de la valve
  }
}

void moveToPosition() {
  Serial.println("Mise en position");
  // Déplacer le moteur jusqu'à ce que le capteur 1 détecte la bonne position
  while (digitalRead(capteurForwardPin) == LOW) {  // Tant que le capteur 1 n'est pas activé
    digitalWrite(motorDirLeftPin, HIGH);                        // Tourner le moteur pour chercher la position
  }
  digitalWrite(motorDirLeftPin, LOW);  // Arrêter le moteur une fois la position atteinte
}

void executeProgram() {
  //Serial.println("Programme en cours");
  if (programSelected == 1) {
    // Programme de rotation et remplissage
    for (int i = 0; i < 3; i++) {
      rotateMotor(direction, turns);
      direction = -direction;  // Change la direction
      addWater(litres);
      litres *= 2;  // Double la quantité d'eau à chaque cycle
    }
  } else if (programSelected == 2) {
    // Programme de rotation uniquement
    for (int i = 0; i < 3; i++) {
      rotateMotor(direction, turns);
      direction = -direction;  // Change la direction
    }
  }

  if (cycleWithEnd && readyForEnd) {
    // Si le cycle de fin est activé, effectuer des étapes supplémentaires
    rotateMotor(direction, turns);
    
   }

  // Fin du programme
  inProgress = false;
  readyForEnd = false;
  digitalWrite(inProgressLedPin, LOW);
  Serial.println("Programme terminé");
}

void rotateMotor(int direction, int turns) {
  Serial.println("Rotation");
  lcd.clear();
  tourCountForward = 0;   // Réinitialiser le compteur de tours avant la rotation
  tourCountBackward = 0;  // Réinitialiser le compteur de tours avant la rotation

 if (direction == 1) {  // Sens avant
    digitalWrite(motorDirLeftPin, HIGH);
    digitalWrite(motorDirRightPin, LOW);

    while (tourCountForward < turns) {  // Attendre que le nombre de tours soit atteint
      if (emergencyStop) {  // Vérifier si l'arrêt d'urgence est activé
        Serial.println("Arrêt d'urgence activé !");
        break;  // Sortir de la boucle en cas d'arrêt d'urgence
      }
      lcd.setCursor(0, 0);
      lcd.print("Nbr de tours : ");
      lcd.print(tourCountForward);
      lcd.print("/");
      lcd.print(turns);
      delay(100);  // Petit délai pour éviter de surcharger le processeur
    }

  } else {  // Sens arrière
    digitalWrite(motorDirLeftPin, LOW);
    digitalWrite(motorDirRightPin, HIGH);

    while (tourCountBackward < turns) {  // Attendre que le nombre de tours arrière soit atteint
      if (emergencyStop) {  // Vérifier si l'arrêt d'urgence est activé
        Serial.println("Arrêt d'urgence activé !");
        break;  // Sortir de la boucle en cas d'arrêt d'urgence
      }
      lcd.setCursor(0, 0);
      lcd.print("Nbr de tours : ");
      lcd.print(tourCountBackward);
      lcd.print("/");
      lcd.print(turns);
      delay(100);  // Petit délai pour éviter de surcharger le processeur
    }
  }

  // Arrêter le moteur après avoir effectué les tours requis
  digitalWrite(motorDirLeftPin, LOW);
  digitalWrite(motorDirRightPin, LOW);
  lcd.clear();
  if (!readyForEnd && !emergencyStop) {         // Programme terminé! Prêt pour le cycle de fin

    readyForEnd = true;
  } else {
    readyForEnd = false;
  }
}

void addWater(int litres) {
  Serial.println("Remplissage");
  flowPulseCount = 0;                                           // Réinitialiser le compteur de litres avant le remplissage
  while (flowPulseCount <= litres) {  // Attendre que le bon nombre de litres soit atteint
    if (emergencyStop) {  // Vérifier si l'arrêt d'urgence est activé
      Serial.println("Arrêt d'urgence activé !");
        break;  // Sortir de la boucle en cas d'arrêt d'urgence
    }
    lcd.setCursor(0, 0);
    lcd.print("Nbr de litres : ");
    lcd.print(flowPulseCount);
    lcd.print("/");
    lcd.print(litres);

    digitalWrite(valve1Pin, HIGH);                      // Ouvre la vanne 1
    digitalWrite(valve2Pin, HIGH);                      // Ouvre la vanne 2
    valve2Open = true;                                  // La vanne 2 est ouverte
  }
  digitalWrite(valve1Pin, LOW);                         // Ferme la vanne 1
  previousMillisValve = millis();                       // Timer pour la fermeture de la vanne 2
  litres = 1;
  lcd.clear();
}

void flowMeterISR() {
  //Serial.print(flowPulseCount);
  unsigned long currentTime = millis();  // Obtenez le temps actuel
  // Vérifiez si le temps écoulé depuis la dernière interruption est suffisant
  if (currentTime - lastInterruptTime > debounceDelay) {
    flowPulseCount++;  // Incrémente le compteur de tours à chaque passage détecté par le capteur inductif
    lastInterruptTime = currentTime;  // Mettez à jour le temps de la dernière interruption
  }
}

// ISR pour compter les tours avec dé-bounce
void countToursForwardISR() {
  //Serial.print(tourCountForward);
  unsigned long currentTime = millis();  // Obtenez le temps actuel
  // Vérifiez si le temps écoulé depuis la dernière interruption est suffisant
  if (currentTime - lastInterruptTime > debounceDelay) {
    tourCountForward++;  // Incrémente le compteur de tours à chaque passage détecté par le capteur inductif
    lastInterruptTime = currentTime;  // Mettez à jour le temps de la dernière interruption
  }
}

// ISR pour compter les tours avec dé-bounce
void countToursBackwardISR() {
  //Serial.print(tourCountBackward);
  unsigned long currentTime = millis();  // Obtenez le temps actuel
  // Vérifiez si le temps écoulé depuis la dernière interruption est suffisant
  if (currentTime - lastInterruptTime > debounceDelay) {
    tourCountBackward++;  // Incrémente le compteur de tours à chaque passage détecté par le capteur inductif
    lastInterruptTime = currentTime;  // Mettez à jour le temps de la dernière interruption
  }
}

void stopMotor() {
  //Serial.println(emergencyStop);
  emergencyStop = true;  // Définir la variable d'arrêt d'urgence
  inProgress = false;
  litres = 1;
   digitalWrite(alarmLedPin, HIGH);
}
