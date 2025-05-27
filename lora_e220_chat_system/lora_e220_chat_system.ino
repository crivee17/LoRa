/**
 * E220 LoRa Chat Application
 * 
 * Sistema di comunicazione chat basato su moduli LoRa E220-30
 * per ESP32 utilizzando la libreria EByte_LoRa_E220_Series_Library
 */

#define E220_30
#include <Arduino.h>
#include <LoRa_E220.h>
#include <EEPROM.h>

// Definizione dei pin di connessione tra ESP32 e modulo E220
#define M0_PIN 27    // Pin M0 del modulo E220
#define M1_PIN 26    // Pin M1 del modulo E220
#define TX_PIN 17    // Pin TX dell'ESP32 connesso al RX del modulo E220
#define RX_PIN 16    // Pin RX dell'ESP32 connesso al TX del modulo E220
#define AUX_PIN 25   // Pin AUX del modulo E220 (per controllo di stato)

// Struttura per salvare le impostazioni in EEPROM
struct SavedSettings {
  uint16_t deviceAddress;  // Indirizzo del dispositivo (2 byte)
  uint8_t channel;         // Canale (1 byte)
  uint8_t airDataRate;     // Velocità trasmissione (1 byte)
  uint8_t txPower;         // Potenza di trasmissione (1 byte)
  char deviceName[20];     // Nome del dispositivo (max 19 caratteri + terminatore)
};

// Impostazioni predefinite
#define DEFAULT_DEVICE_ADDRESS 0x0001
#define DEFAULT_CHANNEL 0x04
#define DEFAULT_AIR_DATA_RATE AIR_DATA_RATE_010_24
#define DEFAULT_TX_POWER POWER_30
#define DEFAULT_DEVICE_NAME "LoRaUser"

// Indirizzo di broadcast per inviare a tutti
#define BROADCAST_ADDRESS 0xFFFF

// Creazione dell'istanza della libreria
LoRa_E220 e220ttl(&Serial2, AUX_PIN, M0_PIN, M1_PIN);

// Variabili globali per le impostazioni correnti
SavedSettings currentSettings;

// Indirizzo EEPROM per le impostazioni
#define EEPROM_SETTINGS_ADDR 0
#define EEPROM_MAGIC_NUMBER 0xE220 // Per verificare se EEPROM è stata già inizializzata
#define EEPROM_SIZE 100

// Buffer per i messaggi in arrivo
#define MAX_MESSAGE_LENGTH 240
char messageBuffer[MAX_MESSAGE_LENGTH];

// Dichiarazione funzioni
void loadSettings();
void saveSettings();
void setupModule();
void printMenu();
void printDeviceInfo();
void handleCommand(char cmd);
void sendMessage(const String &message, uint16_t targetAddress);
bool receiveMessage();
void chatMode();
void configMode();

void setup() {
  // Inizializzazione della serial per il debug e interazione utente
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n=== E220 LoRa Chat Application ===");
  
  // Inizializza EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Carica le impostazioni salvate o usa i valori predefiniti
  loadSettings();
  
  // Inizializzazione della Serial2 per la comunicazione con il modulo
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(500);
  
  Serial.println("Inizializzazione modulo E220...");
  
  // Avvio del modulo
  e220ttl.begin();
  
  // Configura il modulo con le impostazioni correnti
  setupModule();
  
  // Stampa il menu principale
  printMenu();
}

void loop() {
  // Controlla se ci sono comandi dall'utente
  if (Serial.available()) {
    char cmd = Serial.read();
    // Processa solo caratteri validi, ignora caratteri di controllo
    if (cmd >= 32 && cmd <= 126) {  // Caratteri stampabili ASCII
      Serial.print("Comando: ");
      Serial.println(cmd);
      handleCommand(cmd);
    }
  }
  
  // Controlla se ci sono messaggi in arrivo
  if (receiveMessage()) {
    // Messaggi già gestiti nella funzione receiveMessage()
  }
  
  // Breve pausa per evitare sovraccarichi del processore
  delay(50);
}

// Carica le impostazioni da EEPROM
void loadSettings() {
  uint16_t magicNumber;
  EEPROM.get(0, magicNumber);
  
  // Se la EEPROM non è stata inizializzata, usa le impostazioni predefinite
  if (magicNumber != EEPROM_MAGIC_NUMBER) {
    Serial.println("Inizializzazione delle impostazioni predefinite...");
    currentSettings.deviceAddress = DEFAULT_DEVICE_ADDRESS;
    currentSettings.channel = DEFAULT_CHANNEL;
    currentSettings.airDataRate = DEFAULT_AIR_DATA_RATE;
    currentSettings.txPower = DEFAULT_TX_POWER;
    strcpy(currentSettings.deviceName, DEFAULT_DEVICE_NAME);
    
    // Salva le impostazioni predefinite
    saveSettings();
  } else {
    // Carica le impostazioni salvate
    EEPROM.get(2, currentSettings);
    Serial.println("Impostazioni caricate da EEPROM");
  }
}

// Salva le impostazioni in EEPROM
void saveSettings() {
  uint16_t magicNumber = EEPROM_MAGIC_NUMBER;
  EEPROM.put(0, magicNumber);
  EEPROM.put(2, currentSettings);
  EEPROM.commit();
  Serial.println("Impostazioni salvate in EEPROM");
}

// Configura il modulo E220 con le impostazioni correnti
void setupModule() {
  ResponseStructContainer c;
  c = e220ttl.getConfiguration();
  
  if (c.status.code != 1) {
    Serial.println("Errore nella connessione al modulo!");
    Serial.print("Codice errore: ");
    Serial.println(c.status.code);
    return;
  }
  
  // Ottieni la configurazione attuale
  Configuration configuration = *(Configuration*) c.data;
  c.close();
  
  // Imposta la nuova configurazione
  configuration.ADDH = (currentSettings.deviceAddress >> 8) & 0xFF;
  configuration.ADDL = currentSettings.deviceAddress & 0xFF;
  configuration.CHAN = currentSettings.channel;
  
  // Impostazione modalità di trasmissione
  configuration.SPED.airDataRate = currentSettings.airDataRate;
  configuration.SPED.uartParity = MODE_00_8N1;           // 8N1
  configuration.SPED.uartBaudRate = UART_BPS_9600;       // Velocità UART 9600
  
  // Impostazione potenza di trasmissione
  configuration.OPTION.transmissionPower = currentSettings.txPower;
  
  // Modalità di trasmissione
  configuration.TRANSMISSION_MODE.fixedTransmission = FT_FIXED_TRANSMISSION;  // Modalità fixed per indirizzamento
  configuration.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;                  // Abilita RSSI
  configuration.TRANSMISSION_MODE.enableLBT = LBT_DISABLED;                   // Disabilita Listen Before Talk
  configuration.TRANSMISSION_MODE.WORPeriod = WOR_2000_011;                  // Periodo WOR, non usato in trasmissione standard
  
  // Applica la configurazione
  ResponseStatus rs = e220ttl.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
  
  if (rs.code != 1) {
    Serial.println("Errore nella configurazione del modulo!");
    Serial.print("Codice errore: ");
    Serial.println(rs.code);
  } else {
    Serial.println("Configurazione completata con successo!");
    printDeviceInfo();
  }
}

// Stampa il menu principale
void printMenu() {
  Serial.println("\n=== MENU PRINCIPALE ===");
  Serial.println("i - Informazioni sul dispositivo");
  Serial.println("c - Modalità configurazione");
  Serial.println("t - Invia messaggio di test");
  Serial.println("m - Invia messaggio a indirizzo specifico");
  Serial.println("b - Invia messaggio broadcast");
  Serial.println("h - Modalità chat");
  Serial.println("r - Resetta impostazioni predefinite");
  Serial.println("? - Mostra questo menu");
  Serial.println("=======================");
}

// Stampa le informazioni sul dispositivo
void printDeviceInfo() {
  Serial.println("\n=== INFORMAZIONI DISPOSITIVO ===");
  Serial.print("Nome: ");
  Serial.println(currentSettings.deviceName);
  Serial.print("Indirizzo: 0x");
  Serial.println(currentSettings.deviceAddress, HEX);
  Serial.print("Canale: ");
  Serial.println(currentSettings.channel);
  
  String dataRate;
  switch(currentSettings.airDataRate) {
    //case AIR_DATA_RATE_000_03: dataRate = "0.3kbps"; break;
    case AIR_DATA_RATE_001_24: dataRate = "2.4kbps DEF"; break;
    case AIR_DATA_RATE_010_24: dataRate = "2.4kbps"; break;
    case AIR_DATA_RATE_011_48: dataRate = "4.8kbps"; break;
    case AIR_DATA_RATE_100_96: dataRate = "9.6kbps"; break;
    case AIR_DATA_RATE_101_192: dataRate = "19.2kbps"; break;
    case AIR_DATA_RATE_110_384: dataRate = "38.4kbps"; break;
    case AIR_DATA_RATE_111_625: dataRate = "62.5kbps"; break;
    default: dataRate = "Sconosciuto"; break;
  }
  Serial.print("Velocità trasmissione: ");
  Serial.println(dataRate);
  
  String power;
  switch(currentSettings.txPower) {
    case POWER_30: power = "30dBm"; break;
    case POWER_27: power = "27dBm"; break;
    case POWER_24: power = "24dBm"; break;
    case POWER_21: power = "21dBm"; break;
    default: power = "Sconosciuto"; break;
  }
  Serial.print("Potenza di trasmissione: ");
  Serial.println(power);
  Serial.println("==============================");
}

// Funzione di utilità per pulire il buffer di input
void flushSerialInput() {
  while (Serial.available()) {
    Serial.read();
    delay(2);
  }
}

// Funzione di utilità per leggere una stringa dalla seriale, con timeout
String readSerialString(unsigned long timeout = 30000) {
  String result = "";
  unsigned long startTime = millis();
  
  // Pulisci eventuali caratteri già presenti nel buffer
  flushSerialInput();
  
  // Attendi il primo carattere o fino al timeout
  while (!Serial.available() && (millis() - startTime < timeout)) {
    delay(10);
  }
  
  // Se abbiamo caratteri o è scaduto il timeout
  if (Serial.available()) {
    // Leggi fino al carattere newline o fino a quando non ci sono più caratteri
    result = Serial.readStringUntil('\n');
    result.trim(); // Rimuovi spazi e caratteri di controllo
  }
  
  return result;
}

// Funzione di utilità per leggere un singolo carattere dalla seriale, con timeout
char readSerialChar(unsigned long timeout = 30000) {
  unsigned long startTime = millis();
  
  // Pulisci eventuali caratteri già presenti nel buffer
  flushSerialInput();
  
  // Attendi il primo carattere o fino al timeout
  while (!Serial.available() && (millis() - startTime < timeout)) {
    delay(10);
  }
  
  // Se abbiamo un carattere o è scaduto il timeout
  if (Serial.available()) {
    return Serial.read();
  }
  
  return 0; // Ritorna 0 se nessun carattere è disponibile
}

// Gestisce i comandi inseriti dall'utente
void handleCommand(char cmd) {
  // Ignora i caratteri di fine riga
  if (cmd == '\r' || cmd == '\n') {
    return;
  }
  
  switch(cmd) {
    case 'i':
      printDeviceInfo();
      break;
      
    case 'c':
      configMode();
      break;
      
    case 't':
      {
        String testMsg = String(currentSettings.deviceName) + ": Messaggio di test!";
        Serial.println("Invio messaggio di test...");
        sendMessage(testMsg, BROADCAST_ADDRESS);
      }
      break;
      
    case 'm':
      {
        Serial.println("Inserisci l'indirizzo di destinazione (hex, es. 0001):");
        String addrStr = readSerialString();
        
        if (addrStr.length() == 0) {
          Serial.println("Timeout o input vuoto. Operazione annullata.");
          break;
        }
        
        uint16_t addr = strtol(addrStr.c_str(), NULL, 16);
        
        Serial.println("Inserisci il messaggio da inviare:");
        String message = readSerialString();
        
        if (message.length() == 0) {
          Serial.println("Timeout o input vuoto. Operazione annullata.");
          break;
        }
        
        // Prefisso con nome del mittente
        message = String(currentSettings.deviceName) + ": " + message;
        
        sendMessage(message, addr);
      }
      break;
      
    case 'b':
      {
        Serial.println("Inserisci il messaggio broadcast:");
        String message = readSerialString();
        
        if (message.length() == 0) {
          Serial.println("Timeout o input vuoto. Operazione annullata.");
          break;
        }
        
        // Prefisso con nome del mittente
        message = String(currentSettings.deviceName) + ": " + message;
        
        sendMessage(message, BROADCAST_ADDRESS);
      }
      break;
      
    case 'h':
      chatMode();
      break;
      
    case 'r':
      {
        Serial.println("Reset alle impostazioni predefinite? (s/n)");
        char confirm = readSerialChar();
        
        if (confirm == 's' || confirm == 'S') {
          currentSettings.deviceAddress = DEFAULT_DEVICE_ADDRESS;
          currentSettings.channel = DEFAULT_CHANNEL;
          currentSettings.airDataRate = DEFAULT_AIR_DATA_RATE;
          currentSettings.txPower = DEFAULT_TX_POWER;
          strcpy(currentSettings.deviceName, DEFAULT_DEVICE_NAME);
          
          saveSettings();
          setupModule();
          Serial.println("Impostazioni predefinite ripristinate.");
        } else {
          Serial.println("Operazione annullata.");
        }
      }
      break;
      
    case '?':
      printMenu();
      break;
      
    default:
      Serial.println("Comando non riconosciuto. Premi '?' per il menu.");
      break;
  }
}

// Invia un messaggio all'indirizzo specificato
void sendMessage(const String &message, uint16_t targetAddress) {
  if (message.length() > MAX_MESSAGE_LENGTH - 1) {
    Serial.println("Errore: messaggio troppo lungo!");
    return;
  }
  
  // Prefisso il messaggio con l'indirizzo del mittente in formato esadecimale
  // Questo ci permetterà di identificare il mittente anche senza accesso diretto ai dati RSSI
  char addressPrefix[10];
  sprintf(addressPrefix, "[%04X] ", currentSettings.deviceAddress);
  String fullMessage = String(addressPrefix) + message;
  
  ResponseStatus rs;
  if (targetAddress == BROADCAST_ADDRESS) {
    // Broadcast (FFFF)
    rs = e220ttl.sendFixedMessage(0xFF, 0xFF, currentSettings.channel, fullMessage);
    Serial.println("Invio messaggio broadcast...");
  } else {
    // Invio a indirizzo specifico
    rs = e220ttl.sendFixedMessage(targetAddress >> 8, targetAddress & 0xFF, currentSettings.channel, fullMessage);
    Serial.print("Invio messaggio all'indirizzo 0x");
    Serial.print(targetAddress, HEX);
    Serial.println("...");
  }
  
  if (rs.code != 1) {
    Serial.print("Errore nell'invio del messaggio! Codice: ");
    Serial.println(rs.code);
  } else {
    Serial.println("Messaggio inviato con successo!");
  }
}

// Riceve messaggi in arrivo
bool receiveMessage() {
  if (e220ttl.available() > 1) {
    // Ricezione messaggio in modalità fixed con RSSI
    ResponseContainer rc = e220ttl.receiveMessageRSSI();
    
    if (rc.status.code != 1) {
      Serial.print("Errore nella ricezione del messaggio! Codice: ");
      Serial.println(rc.status.code);
      return false;
    }
    
    // Nella modalità fixed, non possiamo ottenere l'indirizzo del mittente direttamente
    // dal messaggio RSSI. Potremmo includere l'indirizzo nel messaggio come parte del protocollo.
    
    // Stampa il messaggio e RSSI
    Serial.print("\n[RSSI: ");
    Serial.print(rc.rssi, DEC);
    Serial.println(" dBm]");
    Serial.println(rc.data);
    
    return true;
  }
  return false;
}

// Modalità chat
void chatMode() {
  Serial.println("\n=== MODALITÀ CHAT ===");
  Serial.println("Inserisci un messaggio e premi invio per inviarlo.");
  Serial.println("Premi 'q' (da solo) e invio per tornare al menu principale.");
  Serial.println("I messaggi in arrivo verranno visualizzati automaticamente.");
  Serial.println("====================");
  
  // Buffer per costruire il messaggio
  String inputBuffer = "";
  
  // Pulisci eventuali caratteri residui nel buffer
  flushSerialInput();
  
  while (true) {
    // Controlla i messaggi in arrivo
    receiveMessage();
    
    // Controlla l'input dell'utente
    if (Serial.available()) {
      char c = Serial.read();
      
      // Se è un carattere di fine riga, processa il messaggio
      if (c == '\n' || c == '\r') {
        if (inputBuffer.length() > 0) {
          // Controlla se l'utente vuole uscire
          if (inputBuffer.equals("q") || inputBuffer.equals("Q")) {
            Serial.println("Uscita dalla modalità chat...");
            inputBuffer = "";
            break;
          }
          
          // Invio messaggio (prefissato con il nome utente)
          String message = String(currentSettings.deviceName) + ": " + inputBuffer;
          sendMessage(message, BROADCAST_ADDRESS);
          
          // Resetta il buffer per il prossimo messaggio
          inputBuffer = "";
          Serial.print("\n> "); // Prompt per nuovo messaggio
        }
      }
      // Gestione del backspace
      else if (c == 8 || c == 127) {  // Backspace o Delete
        if (inputBuffer.length() > 0) {
          inputBuffer.remove(inputBuffer.length() - 1);
          // Effetto visivo del backspace
          Serial.print("\b \b");
        }
      }
      // Aggiungi caratteri validi al buffer
      else if (c >= 32 && c <= 126) {  // Caratteri ASCII stampabili
        inputBuffer += c;
        Serial.print(c);  // Echo del carattere
      }
    }
    
    delay(10);
  }
  
  printMenu();
}

// Modalità configurazione
void configMode() {
  Serial.println("\n=== MODALITÀ CONFIGURAZIONE ===");
  Serial.println("1 - Cambia nome dispositivo");
  Serial.println("2 - Cambia indirizzo dispositivo");
  Serial.println("3 - Cambia canale");
  Serial.println("4 - Cambia velocità di trasmissione");
  Serial.println("5 - Cambia potenza di trasmissione");
  Serial.println("6 - Salva e applica configurazione");
  Serial.println("0 - Torna al menu principale");
  Serial.println("=============================");
  
  SavedSettings tempSettings = currentSettings;  // Copia temporanea per le modifiche
  
  // Pulisci il buffer di input
  flushSerialInput();
  
  while (true) {
    if (Serial.available()) {
      char option = Serial.read();
      
      // Ignora caratteri di controllo
      if (option == '\r' || option == '\n') {
        continue;
      }
      
      Serial.print("Opzione selezionata: ");
      Serial.println(option);
      
      switch(option) {
        case '1':
          {
            Serial.println("Inserisci nuovo nome dispositivo (max 19 caratteri):");
            String nameStr = readSerialString();
            
            if (nameStr.length() > 0 && nameStr.length() < 20) {
              nameStr.toCharArray(tempSettings.deviceName, 20);
              Serial.print("Nuovo nome: ");
              Serial.println(tempSettings.deviceName);
            } else {
              Serial.println("Nome non valido o troppo lungo!");
            }
          }
          break;
          
        case '2':
          {
            Serial.println("Inserisci nuovo indirizzo dispositivo (hex, es. 0001):");
            String addrStr = readSerialString();
            
            if (addrStr.length() > 0) {
              uint16_t addr = strtol(addrStr.c_str(), NULL, 16);
              tempSettings.deviceAddress = addr;
              Serial.print("Nuovo indirizzo: 0x");
              Serial.println(tempSettings.deviceAddress, HEX);
            } else {
              Serial.println("Indirizzo non valido!");
            }
          }
          break;
          
        case '3':
          {
            Serial.println("Inserisci nuovo canale (0-31):");
            String chanStr = readSerialString();
            
            int chan = chanStr.toInt();
            if (chan >= 0 && chan <= 31) {
              tempSettings.channel = chan;
              Serial.print("Nuovo canale: ");
              Serial.println(tempSettings.channel);
            } else {
              Serial.println("Canale non valido! Deve essere tra 0 e 31.");
            }
          }
          break;
          
        case '4':
          {
            Serial.println("Seleziona velocità di trasmissione:");
            Serial.println("0 - 0.3 kbps");
            Serial.println("1 - 1.2 kbps");
            Serial.println("2 - 2.4 kbps");
            Serial.println("3 - 4.8 kbps");
            Serial.println("4 - 9.6 kbps");
            Serial.println("5 - 19.2 kbps");
            Serial.println("6 - 38.4 kbps");
            Serial.println("7 - 62.5 kbps");
            
            char rateOption = readSerialChar();
            uint8_t airRate;
            
            switch(rateOption) {
              //case '0': airRate = AIR_DATA_RATE_000_03; break;
              //case '1': airRate = AIR_DATA_RATE_001_12; break;
              case '2': airRate = AIR_DATA_RATE_010_24; break;
              case '3': airRate = AIR_DATA_RATE_011_48; break;
              case '4': airRate = AIR_DATA_RATE_100_96; break;
              case '5': airRate = AIR_DATA_RATE_101_192; break;
              case '6': airRate = AIR_DATA_RATE_110_384; break;
              case '7': airRate = AIR_DATA_RATE_111_625; break;
              default:
                Serial.println("Opzione non valida!");
                continue;
            }
            
            tempSettings.airDataRate = airRate;
            Serial.println("Velocità impostata.");
          }
          break;
          
        case '5':
          {
            Serial.println("Seleziona potenza di trasmissione:");
            Serial.println("1 - 21 dBm");
            Serial.println("2 - 24 dBm");
            Serial.println("3 - 27 dBm");
            Serial.println("4 - 30 dBm");
            
            char powerOption = readSerialChar();
            uint8_t txPower;
            
            switch(powerOption) {
              case '1': txPower = POWER_21; break;
              case '2': txPower = POWER_24; break;
              case '3': txPower = POWER_27; break;
              case '4': txPower = POWER_30; break;
              default:
                Serial.println("Opzione non valida!");
                continue;
            }
            
            tempSettings.txPower = txPower;
            Serial.println("Potenza impostata.");
          }
          break;
          
        case '6':
          {
            // Applica le impostazioni temporanee
            currentSettings = tempSettings;
            saveSettings();
            setupModule();
            Serial.println("Configurazione salvata e applicata.");
            return;  // Torna al menu principale
          }
          break;
          
        case '0':
          Serial.println("Uscita senza salvare...");
          printMenu();
          return;  // Torna al menu principale
          
        default:
          Serial.println("Opzione non valida! Riprova.");
          break;
      }
      
      // Mostra nuovamente il menu di configurazione
      Serial.println("\n=== MODALITÀ CONFIGURAZIONE ===");
      Serial.println("1 - Cambia nome dispositivo");
      Serial.println("2 - Cambia indirizzo dispositivo");
      Serial.println("3 - Cambia canale");
      Serial.println("4 - Cambia velocità di trasmissione");
      Serial.println("5 - Cambia potenza di trasmissione");
      Serial.println("6 - Salva e applica configurazione");
      Serial.println("0 - Torna al menu principale");
      Serial.println("=============================");
    }
    
    // Controlla se ci sono messaggi in arrivo anche in modalità configurazione
    receiveMessage();
    
    // Breve pausa
    delay(50);
  }
}