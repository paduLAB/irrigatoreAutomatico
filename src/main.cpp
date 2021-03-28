
/*
    Progetto 2 Nodo Singolo Remoto

    Codice del progetto

    Lorenzo Neri
*/

// Includiamo le librerie

#include <WiFiUdp.h>
#include <WiFi.h>
#include <Time.h>
#include "NTPClient.h"
#include <HTTPClient.h>

#include "definitions.h"
#include "gen_func.h"



// Nome di questo nodo

String nomeNodo = "Nodo1";

// Endpoint email
String endpoint_email = "http://www.padulab.it/nodoremoto/send_email_event.php";

// Endpoint API
String endpoint_API = "http://www.padulab.it/nodoremoto/api.php";

// Variabile per salvare l'indirizzo IP del nodo
String indirizzoIP;

String dataOraCorrente;

// Creiamo il client UDP per ottenere data e ora tramite server NTP
WiFiUDP ntpUDP;
NTPClient dataoraClient(ntpUDP, "pool.ntp.org");

enum {
init_st, measure_st, takeTime_st, send_st, goToSleep_st
};

static uint8_t  phase=init_st;
static uint32_t global_timer=0;

void sendEmailAlert();
String getDataOraCorrente(NTPClient client);

const int pwmSensorPin = 16;
const int readSensorPin = A0;

const int freq = 50000;
const int sensorChannel = 2;
const int resolution = 8;

int32_t delta_sensor_value=0;
int32_t mean_sensor_value=0;
uint16_t n_sensor_value=0;

void setup()
{
  Serial.begin(115200);

  // Ci connettiamo alla rete Wi-Fi
  Serial.print("Mi sto connettendo alla rete Wi-Fi ");
  WiFi.mode(WIFI_STA);

  WiFi.begin(SSID, PASSWORD);

  wl_status_t wifi_status = WiFi.status();
  while (WiFi.status() != WL_CONNECTED)
  {
    wifi_status = WiFi.status();
    if (wifi_status == WL_CONNECT_FAILED)
      esp_restart();
  }
  // Connessione avvenuta con successo. Mostriamo l'indirizzo IP
  Serial.println("");
  Serial.println("Connesso alla rete.");
  Serial.println("Il mio indirizzo IP è: ");

  // Il metodo localIP restituisce un vettore di 4 celle. Ognuna contiene parte dell'indirizzo IP
  // Ognuna va convertita in stringa

  indirizzoIP = String(WiFi.localIP()[0]) + "." + String(WiFi.localIP()[1]) + "." + String(WiFi.localIP()[2]) + "." + String(WiFi.localIP()[3]);
  Serial.println(indirizzoIP);

  //Inizializziamo il client per ottenere data e ora
  dataoraClient.begin();
  // In base al nostro fuso orario, impostiamo l'offset in secondi.
  // Noi (Italia) ci troviamo in GMT + 1 perciò sono 3600 secondi

  dataoraClient.setTimeOffset(3600);
  
  ledcSetup(sensorChannel, freq, resolution);
  ledcAttachPin(pwmSensorPin, sensorChannel);
  ledcWrite(sensorChannel, 150);
}

void loop()
{
  switch(phase)
  {
    case init_st:
      Serial.println("fase 0");
      mean_sensor_value=0;
      resetTimer(&global_timer);
      phase++;
    break;
    
    case measure_st:
    #define N_SAMPLES 100
    {
      uint32_t time=getTime(global_timer);
      Serial.println("fase 1: ");
      if(time>(uint32_t)10000){
        for(int i=0;i<N_SAMPLES;i++){
          Serial.println("N : "+ String(n_sensor_value));
          n_sensor_value++;
          delta_sensor_value=(int32_t)(analogRead(readSensorPin)-mean_sensor_value);
          mean_sensor_value+=(delta_sensor_value/n_sensor_value);
        }
        Serial.println("value:"+String(mean_sensor_value));
        phase++;
      }
    }
    break;

    case takeTime_st:
      Serial.println("fase 2");
      dataOraCorrente = getDataOraCorrente(dataoraClient);
      phase++;
    break;

    case send_st:
    {
      Serial.println("fase 3");
      //Prepara messaggio da POSTARE
      String datiPOST = "valore_sensore=" + String(mean_sensor_value) + "&data_ora_creazione=" + dataOraCorrente + "&nome_nodo=" + nomeNodo + "&ip_nodo=" + indirizzoIP;
      // Impostiamo il client HTTP per eseguire la richiesta
      HTTPClient richiestaHTTP;
      richiestaHTTP.begin(endpoint_API);
      richiestaHTTP.addHeader("Content-Type", "application/x-www-form-urlencoded");
      // Creiamo una variabile per salvare il codice della risposta (se è 200 è andata a buon fine!)
      auto rispostaCodiceHTTP = richiestaHTTP.POST(datiPOST);
      // Creiamo una variabile per salvare il testo della risposta: in altre parole quello che abbiamo messo come risposta nel file PHP dell'API
      richiestaHTTP.getString();
      // Chiudiamo la richiesta
      richiestaHTTP.end();
      phase++;
    }
    break;

    case goToSleep_st:
      Serial.println("fase 4");
      esp_sleep_enable_timer_wakeup(SLEEP_TIME);
      esp_deep_sleep_start();
      phase++;
    break;
    

    default:
    break;
  }




}

void sendEmailAlert()
{
  // Impostiamo il client HTTP per eseguire la richiesta

  HTTPClient richiestaHTTP;
  richiestaHTTP.begin(endpoint_email + "?send_email=1");
  richiestaHTTP.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Creiamo una variabile per salvare il codice della risposta (se è 200 è andata a buon fine!)
  auto rispostaCodiceHTTP = richiestaHTTP.GET();
  Serial.println(rispostaCodiceHTTP);

  // Creiamo una variabile per salvare il testo della risposta: in altre parole quello che abbiamo messo come risposta nel file PHP dell'API
  String rispostaPayload = richiestaHTTP.getString();
  Serial.println(rispostaPayload);

  // Chiudiamo la richiesta
  richiestaHTTP.end();
}

String getDataOraCorrente(NTPClient client){
  //Otteniamo data e ora di questo istante

  client.update();
// Struct per il tempo: serve per ottenere l'anno corrente
  unsigned long epochTime = client.getEpochTime();

  struct tm *ptm = gmtime((time_t *)&epochTime);

  // Otteniamo le ore

  String oreCorrenti = client.getFormattedTime();

  // Giorno

  int giornoCorrente = ptm->tm_mday;

  // Aggiungiamo uno 0 se è minore di 10 e lo trasformiamo in stringa

  String giornoCorrenteStringa = String(giornoCorrente);

  if (giornoCorrente < 10)
  {
    giornoCorrenteStringa = "0" + giornoCorrenteStringa;
  }

  // Mese corrente

  int meseCorrente = ptm->tm_mon + 1;

  // Stesso concetto per il mese

  String meseCorrenteStringa = String(meseCorrente);

  if (meseCorrente < 10)
  {
    meseCorrenteStringa = "0" + meseCorrenteStringa;
  }

  // Anno corrente

  int annoCorrente = ptm->tm_year + 1900;

  // Data nel formato YYYY-MM-DD

  String dataCorrente = String(annoCorrente) + "-" + meseCorrenteStringa + "-" + giornoCorrenteStringa;

  // Uniamo data e ora nel formato YYYY-MM-DD HH:MM:SS

  String dataOraCorrente = dataCorrente + " " + oreCorrenti;

  return dataOraCorrente;
  
}
