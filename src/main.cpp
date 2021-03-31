
// Includiamo le librerie

#include <WiFiUdp.h>
#include <WiFi.h>
#include <Time.h>
#include "NTP.h"
#include <HTTPClient.h>
#include "RTClib.h"
#include <EEPROM.h>

#include "definitions.h"
#include "gen_func.h"

RTC_DS3231 rtc;

// Variabile per salvare l'indirizzo IP del nodo
String indirizzoIP;

String dataOraCorrente;

// Creiamo il client UDP per ottenere data e ora tramite server NTP
WiFiUDP ntpUDP;
NTP ntp(ntpUDP);

enum
{
  init_st,
  measure_st,
  takeTime_st,
  send_st,
  goToSleep_st
};

static uint8_t phase = init_st;
static uint32_t global_timer = 0;

int32_t delta_sensor_value = 0;
int32_t mean_sensor_value = 0;
uint16_t n_sensor_value = 0;

void sendEmailAlert();
String getDataEOra(struct tm *ptm);
struct tm dataTimeToTM(DateTime datetime);

void setup()
{
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  if (rtc.begin())
    Serial.println("\nRTC_OK");
  else
    Serial.println("RTC_KO");

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
  Serial.println("WIFI_OK");

  // Il metodo localIP restituisce un vettore di 4 celle. Ognuna contiene parte dell'indirizzo IP
  // Ognuna va convertita in stringa
  //  Serial.println("Il mio indirizzo IP è: ");
  indirizzoIP = String(WiFi.localIP()[0])+ "."  + String(WiFi.localIP()[1]) + "." + String(WiFi.localIP()[2]) + "." + String(WiFi.localIP()[3]);
  //Serial.println(indirizzoIP);
  if (EEPROM.read(CLOCK_SET_ADDR) != 0x0C)
  {
    //TIME ZONE HANDLER
    ntp.ruleDST("CEST", Last, Sun, Mar, 2, 120); // last sunday in march 2:00, timetone +120min (+1 GMT + 1h summertime offset)
    ntp.ruleSTD("CET", Last, Sun, Oct, 3, 60);   // last sunday in october 3:00, timezone +60min (+1 GMT)
    ntp.begin();
    ntp.isDST(true);
    ntp.update();
    rtc.disable32K();
    rtc.adjust(DateTime(ntp.year() + 100, ntp.month(), ntp.day(), ntp.hours(), ntp.minutes(), ntp.seconds()));
    EEPROM.write(CLOCK_SET_ADDR, 0x01);
    while (!EEPROM.commit());
    Serial.println("RTC setted");

    DateTime Alarm_every_15min(0, 0, 0, 0, 15, 0);
    if (rtc.setAlarm1(Alarm_every_15min, DS3231_A1_Minute))
      Serial.println("Alarm set");
    else
      Serial.println("Alarm set error");
  }

  ledcSetup(PWM_SENSOR_CHANNEL, PWM_SENSOR_FREQ, PWM_SENSOR_RESOLUTION);
  ledcAttachPin(PWM_SENSOR_PIN, PWM_SENSOR_CHANNEL);
  ledcWrite(PWM_SENSOR_CHANNEL, 150);
}

void loop()
{
  switch (phase)
  {
  case init_st:
    Serial.println("fase 0");
    mean_sensor_value = 0;
    resetTimer(&global_timer);
    phase++;
    break;

  case measure_st:
#define N_SAMPLES 100
  {
    uint32_t time = getTime(global_timer);
    Serial.println("fase 1: ");
    if (time > (uint32_t)10000)
    {
      for (int i = 0; i < N_SAMPLES; i++)
      {
        Serial.println("N : " + String(n_sensor_value));
        n_sensor_value++;
        delta_sensor_value = (int32_t)(analogRead(READ_SENSOR_PIN) - mean_sensor_value);
        mean_sensor_value += (delta_sensor_value / n_sensor_value);
      }
      Serial.println("value:" + String(mean_sensor_value));
      phase++;
    }
  }
  break;

  case takeTime_st:
    Serial.println("fase 2");
    static struct tm ptm2 = dataTimeToTM(rtc.now());
    dataOraCorrente = getDataEOra(&ptm2);
    phase++;
    break;

  case send_st:
  {
    Serial.println("fase 3");
    //Prepara messaggio da POSTARE
    String datiPOST = "valore_sensore=" + String(mean_sensor_value) + "&data_ora_creazione=" + dataOraCorrente + "&nome_nodo=" + NOME_NODO + "&ip_nodo=" + indirizzoIP;
    // Impostiamo il client HTTP per eseguire la richiesta
    HTTPClient richiestaHTTP;
    richiestaHTTP.begin(ENDPOINT_API);
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
    rtc.clearAlarm(1);
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
  richiestaHTTP.begin(String(ENDPOINT_EMAIL) + "?send_email=1");
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

String getDataEOra(struct tm *ptm)
{

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

  String oreCorrentiStringa = String(ptm->tm_hour);
  String minutiCorrentiStringa = String(ptm->tm_min);
  String secondiCorrentiStringa = String(ptm->tm_sec);

  if (ptm->tm_hour < 10)
  {
    oreCorrentiStringa = "0" + oreCorrentiStringa;
  }

  if (ptm->tm_min < 10)
  {
    minutiCorrentiStringa = "0" + minutiCorrentiStringa;
  }

  if (ptm->tm_sec < 10)
  {
    secondiCorrentiStringa = "0" + secondiCorrentiStringa;
  }

  String orarioCorrente = oreCorrentiStringa + ":" + minutiCorrentiStringa + ":" + secondiCorrentiStringa;

  String dataOraCorrente = dataCorrente + " " + orarioCorrente;

  return dataOraCorrente;
}

struct tm dataTimeToTM(DateTime datetime)
{

  struct tm datetime_tm;

  datetime_tm.tm_year = (int)datetime.year() - 2000;
  datetime_tm.tm_mon = datetime.month() - 1;
  datetime_tm.tm_mday = datetime.day();
  datetime_tm.tm_hour = datetime.hour();
  datetime_tm.tm_min = datetime.minute();
  datetime_tm.tm_sec = datetime.second();

  return datetime_tm;
}