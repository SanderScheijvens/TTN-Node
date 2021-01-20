#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

// De DHT22 Library
#include "DHT.h"


#include <Wire.h>
#include <Adafruit_GFX.h>
#include <ssd1306.h>

// DHT digital pin en sensor type
#define DHTPIN 23
#define DHTTYPE DHT22

//OLED pin map voor Heltec Board
#ifndef OLED_RST
  #define OLED_RST 16
#endif
#ifndef OLED_SDA
  #define OLED_SDA 4
#endif
#ifndef OLED_SCL
  #define OLED_SCL 15
#endif

SSD1306 display(OLED_RST);

#if (SSD1306_LCDHEIGHT != 64)
 #error("Height incorrect, please fix ssd1306.h!");
#endif

// Deze EUI moet de indeling little-endian hebben, dus de minst significante byte
// Dus, bij het kopiëren van een EUI van ttn, betekent dit het omgekeerde van big-endian
// t Voor TTN uitgegeven EUI's moeten de laatste bytes 0xD5, 0xB3, 0x70 zijn.
static const u1_t PROGMEM APPEUI[8] = { "YOUR APPEUI HERE" };
void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8);}

// Dit moet ook in het format little endian zijn, zie hierboven.
static const u1_t PROGMEM DEVEUI[8] = { "YOUR DEVEUI HERE" };
void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8);}

// Deze sleutel moet de indeling Big Endian hebben (of, aangezien het niet echt een nummer 
// is maar een geheugenblok, is endianness niet echt van toepassing). In de praktijk kan 
// een sleutel die uit de TTN-console wordt gehaald, ongewijzigd worden gekopieerd.
static const u1_t PROGMEM APPKEY[16] = { "YOUR APPKEY HERE" };
void os_getDevKey (u1_t* buf) {  memcpy_P(buf, APPKEY, 16);}

// payload om naar TTN-gateway te verzenden
static uint8_t payload[5];
static osjob_t sendjob;

// TX om de zoveel seconden (kan langer worden vanwege beperkingen in de werkcyclus).
const unsigned TX_INTERVAL = 10;

// dht
float temperature;
float rHumidity;

// Pin mapping voor Heltec WiFi LoRa Board
const lmic_pinmap lmic_pins = {
    .nss = 18,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 14,
    .dio = {26, 33, 32},
    .rxtx_rx_active = 0,
    .rssi_cal = 8,
    .spi_freq = 8000000,
};

// init. DHT
DHT dht(DHTPIN, DHTTYPE);

void onEvent (ev_t ev) {
    Serial.print(os_getTime());
    Serial.print(": ");
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            Serial.println(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            Serial.println(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            Serial.println(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            display.print("TTN: Joining...");
            display.display();
            Serial.println(F("EV_JOINING"));
            break;
        case EV_JOINED:
            display.clearDisplay();
            display.display();
            display.setCursor(0, 0);
            display.println("TTN: Connected");
            display.display();
            Serial.println(F("EV_JOINED"));
            {
              u4_t netid = 0;
              devaddr_t devaddr = 0;
              u1_t nwkKey[16];
              u1_t artKey[16];
              LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
              Serial.print("netid: ");
              Serial.println(netid, DEC);
              Serial.print("devaddr: ");
              Serial.println(devaddr, HEX);
              Serial.print("artKey: ");
              for (int i=0; i<sizeof(artKey); ++i) {
                if (i != 0)
                  Serial.print("-");
                Serial.print(artKey[i], HEX);
              }
              Serial.println("");
              Serial.print("nwkKey: ");
              for (int i=0; i<sizeof(nwkKey); ++i) {
                      if (i != 0)
                              Serial.print("-");
                      Serial.print(nwkKey[i], HEX);
              }
              Serial.println("");
            }
    
            LMIC_setLinkCheckMode(0);
            break;

        case EV_JOIN_FAILED:
            Serial.println(F("EV_JOIN_FAILED"));
            break;
        case EV_REJOIN_FAILED:
            Serial.println(F("EV_REJOIN_FAILED"));
            break;
            break;
        case EV_TXCOMPLETE:
            display.clearDisplay();
            display.display();
            display.setCursor(0, 0);
            display.println("TTN: Connected");
            display.display();
            display.setCursor(0, 20);
            display.println("* Sent!");
            display.display();   
            Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            if (LMIC.txrxFlags & TXRX_ACK)
              Serial.println(F("Received ack"));
            if (LMIC.dataLen) {
              Serial.println(F("Received "));
              Serial.println(LMIC.dataLen);
              Serial.println(F(" bytes of payload"));
            }
            // Planning de volgende verzending
            os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
            break;
        case EV_LOST_TSYNC:
            Serial.println(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            Serial.println(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            // gegevens ontvangen in ping-slot
            Serial.println(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            Serial.println(F("EV_LINK_DEAD"));
            break;
        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            break;

        case EV_TXSTART:
            display.clearDisplay();
            display.display();
            display.setCursor(0, 0);
            display.println("TTN: Connected");
            display.setCursor(0, 10);
            display.println("* Sending");
            display.setCursor(0, 25);
            display.print("Temp: ");display.print(temperature*100);display.print(" C, ");
            display.print("RH%: ");display.print(rHumidity*100);
            display.display();
            Serial.println(F("EV_TXSTART"));
            break;
        default:
            Serial.print(F("Unknown event: "));
            Serial.println((unsigned) ev);
            break;
    }
}

void do_send(osjob_t* j){
    // Controleer of er momenteel geen TX / RX-taak wordt uitgevoerd
    if (LMIC.opmode & OP_TXRXPEND) {
        Serial.println(F("OP_TXRXPEND, not sending"));
    } else {
        // meting temperatuur starten
        temperature = dht.readTemperature();
        Serial.print("Temperature: "); Serial.print(temperature);
        Serial.println(" *C");
        // bijwerken
        temperature = temperature / 100; 

        // meting luchtvochtigheid starten
        rHumidity = dht.readHumidity();
        Serial.print("%RH ");
        Serial.println(rHumidity);
        // bijwerken
        rHumidity = rHumidity / 100;
        
        // van float -> int
        uint16_t payloadTemp = LMIC_f2sflt16(temperature);
        // van int -> bytes
        byte tempLow = lowByte(payloadTemp);
        byte tempHigh = highByte(payloadTemp);
        // bytes in de payload
        payload[0] = tempLow;
        payload[1] = tempHigh;

        // van float -> int
        uint16_t payloadHumid = LMIC_f2sflt16(rHumidity);
        // van int -> bytes
        byte humidLow = lowByte(payloadHumid);
        byte humidHigh = highByte(payloadHumid);
        payload[2] = humidLow;
        payload[3] = humidHigh;

        // bereid upstream datatransmissie voor op het eerst mogelijke tijdstip. 
        // verzenden op poort 1 (de eerste parameter); u kunt elke waarde tussen 1 en 223 gebruiken 
        // (andere zijn gereserveerd). vraag geen ack aan. Onthoud dat acks veel netwerkbronnen verbruiken; 
        // vraag niet om een ack, tenzij je het echt nodig hebt.
        LMIC_setTxData2(1, payload, sizeof(payload)-1, 0);
        Serial.println(F("EV_TXSTART"));
    }
    // De volgende TX is gepland na de TX_COMPLETE-gebeurtenis.
}

void setup() {
    delay(5000);
    while (! Serial);
    Serial.begin(9600);
    Serial.println(F("Starting"));

    dht.begin();
    Wire.begin(OLED_SDA,OLED_SCL,100000);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    Serial.println("OLED and DHT init'd");

    display.display();
    delay(1000);
   
    display.clearDisplay();
    display.display();

    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0,0);

    os_init();
    // Reset de MAC-status. Sessie- en lopende gegevensoverdrachten worden geweigerd.
    LMIC_reset();
    // Schakel de linkcontrolemodus en ADR uit, omdat ADR het testen vaak ingewikkelder maakt.
    LMIC_setLinkCheckMode(0);
    LMIC_setDrTxpow(DR_SF7,14);
    #if defined(CFG_eu868)
    
    // kanaal instellingen  
    LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI);      // g-band
    LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(DR_FSK,  DR_FSK),  BAND_MILLI);      // g2-band
    #elif defined(CFG_us915)
    LMIC_selectSubBand(1);
    #endif

    // Start job (automatisch verzenden, start OTAA ook)
    do_send(&sendjob);
}

void loop() {
  // we noemen de runloop-processor van de LMIC. Hierdoor gebeuren er dingen op basis van gebeurtenissen en tijd. Een 
  // van de dingen die zullen gebeuren, zijn callbacks voor verzending van complete of ontvangen berichten. We 
  // gebruiken deze lus ook om periodieke datatransmissies in de wachtrij te plaatsen. Je kunt hier andere dingen in de `loop ()` routine zetten, 
  // maar pas op dat de LoRaWAN-timing behoorlijk krap is, dus als je meer dan een paar milliseconden aan werk doet, wil je 
  // `os_runloop_once ()` aanroepen om de zoveel tijd, om de radio draaiende te houden.
  os_runloop_once();
}
