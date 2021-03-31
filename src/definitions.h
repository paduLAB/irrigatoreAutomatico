#ifndef DEFINITIONS_H_
#define DEFINITIONS_H_

#define MS_TO_US    1000UL
#define S_TO_MS     1000UL
#define M_TO_MS     60UL * S_TO_MS

#define SLEEP_TIME  60UL*M_TO_MS*MS_TO_US

// Nome Rete Wi-Fi e relativa password
#define SSID            "#andràtuttobere"
#define PASSWORD       "ciucciomio"

// Nome di questo nodo
#define NOME_NODO       "Nodo1"

// Endpoint email
#define ENDPOINT_EMAIL  "http://www.padulab.it/nodoremoto/send_email_event.php"

// Endpoint API
#define ENDPOINT_API    "http://www.padulab.it/nodoremoto/api.php"

//PIN DEFINITION
#define PWM_SENSOR_PIN        16
#define READ_SENSOR_PIN       A0

#define PWM_SENSOR_FREQ       50000
#define PWM_SENSOR_CHANNEL    2
#define PWM_SENSOR_RESOLUTION 8

//EEPROM
#define EEPROM_SIZE     1
#define CLOCK_SET_ADDR  0

#endif //DEFINITIONS_H_