// Includiamo le librerie

#include <WiFiUdp.h>
#include <WiFi.h>
#include <Time.h>
#include "NTP.h"
#include <HTTPClient.h>
#include "RTClib.h"
#include <EEPROM.h>
#include "DHT.h"

#include "definitions.h"
#include "gen_func.h"

#define RET_OK    0x01
#define RET_ERR   0xFF
#define RET_BUSY  0xA0

RTC_DS3231 rtc;

// Variabile per salvare l'indirizzo IP del nodo
String macAddress;
String dataOraCorrente;

// Creiamo il client UDP per ottenere data e ora tramite server NTP
WiFiUDP ntpUDP;
NTP ntp(ntpUDP);

uint8_t computePM(uint16_t *pm_value, uint8_t pin);

enum
{
  init_st,
  measure_h2s_st,
  measure_pm_st,
  measure_temperature_humidity_st,
  preparePacket_st,
  send_st,
  putInMem_st,
  goToSleep_st
};

static uint8_t phase = init_st;
static uint32_t global_timer = 0;

uint32_t h2s_raw_value, nox_raw_value, red_raw_value;
uint16_t pm2_5_val, pm10_val;
int8_t humidity_val;
int16_t temperature_val;

bool isWifiOk = false;

void sendEmailAlert();
String getDataEOra(struct tm *ptm);
String datiPOST;
struct tm dataTimeToTM(DateTime datetime);
int32_t computeAnalogMeanValue(uint8_t analogPin);

DHT dht(DHT11_SENSOR_PIN, DHT11);

void setup()
{

  pinMode(PM10_SENSOR_PIN, INPUT);
  pinMode(PM2_5_SENSOR_PIN, INPUT);
  dht.begin();

  time_t now;
  struct tm *now_tm;

  now = time(NULL);
  now_tm = localtime(&now);

  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  if (rtc.begin())
    Serial.println("\nRTC_OK");
  else
  {
    Serial.println("RTC_KO");
    while (1)
      ;
  }

  // Ci connettiamo alla rete Wi-Fi
  Serial.print("Mi sto connettendo alla rete Wi-Fi ");
  WiFi.mode(WIFI_STA);

  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() == WL_DISCONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  wl_status_t wifi_status = WiFi.status();
  Serial.print(wifi_status);
  if (wifi_status == WL_CONNECTED)
    isWifiOk = true;

  // Connessione avvenuta con successo. Mostriamo l'indirizzo IP
  Serial.println("WIFI: " + String(isWifiOk));

#define EEPROM_SIGNATURE (uint8_t)(now_tm->tm_sec + now_tm->tm_min + now_tm->tm_hour)

  // Il metodo localIP restituisce un vettore di 4 celle. Ognuna contiene parte dell'indirizzo IP
  // Ognuna va convertita in stringa
  //  Serial.println("Il mio indirizzo IP è: ");
  macAddress = String(WiFi.macAddress()[0], HEX) + "." + String(WiFi.macAddress()[1], HEX) + "." + String(WiFi.macAddress()[2], HEX) + "." + String(WiFi.macAddress()[3], HEX) + "." + String(WiFi.macAddress()[4], HEX) + "." + String(WiFi.macAddress()[5], HEX);
  // Serial.println(indirizzoIP);
  if (EEPROM.read(CLOCK_SET_ADDR) != EEPROM_SIGNATURE)
  {
    // TIME ZONE HANDLER
    ntp.ruleDST("CEST", Last, Sun, Mar, 2, 120); // last sunday in march 2:00, timetone +120min (+1 GMT + 1h summertime offset)
    ntp.ruleSTD("CET", Last, Sun, Oct, 3, 60);   // last sunday in october 3:00, timezone +60min (+1 GMT)
    ntp.begin();
    ntp.isDST(true);
    ntp.update();
    rtc.enable32K();
    rtc.adjust(DateTime(ntp.year() + 100, ntp.month(), ntp.day(), ntp.hours(), ntp.minutes(), ntp.seconds()));

    Serial.println("RTC setted");

    DateTime Alarm_morning(0, 0, 0, 23, 12, 0);
    DateTime Alarm_night(0, 0, 0, 7, 14, 0);
    if ((rtc.setAlarm1(Alarm_morning, DS3231_A1_Hour)) && (rtc.setAlarm2(Alarm_night, DS3231_A2_Hour)))
    {
      Serial.println("Alarm set");
      EEPROM.write(CLOCK_SET_ADDR, EEPROM_SIGNATURE);
      while (!EEPROM.commit())
        ;
    }
    else
      Serial.println("Alarm set error");
  }

  HTTPClient fwInfoRequest;
  fwInfoRequest.begin(String(UPDATE_INFO_URL) + "?fwInfoReq=major");
  fwInfoRequest.GET();
  uint8_t major = (uint8_t)(fwInfoRequest.getString()).toInt();
  if (major >= FW_VERSION_MAJOR)
  {
    fwInfoRequest.setURL(String(UPDATE_INFO_URL) + "?fwInfoReq=minor");
    fwInfoRequest.GET();
    uint8_t minor = (uint8_t)(fwInfoRequest.getString()).toInt();
    if (minor >= FW_VERSION_MINOR)
    {
      fwInfoRequest.setURL(String(UPDATE_INFO_URL) + "?fwInfoReq=patch");
      fwInfoRequest.GET();
      uint8_t patch = (uint8_t)(fwInfoRequest.getString()).toInt();
      if (patch > FW_VERSION_PATCH)
      {
        fwInfoRequest.setURL(String(UPDATE_INFO_URL) + "?fwInfoReq=url");
        fwInfoRequest.GET();
        String url = fwInfoRequest.getString();
        ota_client_config.url = url.c_str();
        ota_client_config.cert_pem = server_cert_pem_start;

        esp_err_t ret = esp_https_ota(&ota_client_config);
        if (ret == ESP_OK)
        {

          printf("OTA OK, restarting...\n");
          esp_restart();
        }
        else
        {
          printf("OTA failed...\n");
        }
      }
    }
  }
  fwInfoRequest.end();
}

void loop()
{
  switch (phase)
  {
  case init_st:
    Serial.println("fase 0");
    h2s_raw_value = 0;
    nox_raw_value = 0;
    red_raw_value = 0;
    resetTimer(&global_timer);
    phase++;
    break;

  case measure_h2s_st:

  {
    uint32_t time = getTime(global_timer);
    if (time > SENSOR_INIT_WAIT)
    {
      h2s_raw_value = analogReadMilliVolts(ADC_H2S_SENSOR_PIN);
      nox_raw_value = analogReadMilliVolts(ADC_NOX_SENSOR_PIN);
      red_raw_value = analogReadMilliVolts(ADC_RED_SENSOR_PIN);
      Serial.println("h2s_raw:" + String(h2s_raw_value));
      Serial.println("nox_raw:" + String(nox_raw_value));
      Serial.println("red_raw:" + String(red_raw_value));

      static struct tm ptm2 = dataTimeToTM(rtc.now());
      dataOraCorrente = getDataEOra(&ptm2);

      Serial.println("data ora:" + String(dataOraCorrente));
      phase++;
    }
  }
  break;

  case measure_pm_st:
  {
    while (computePM(&pm2_5_val, PM2_5_SENSOR_PIN) == RET_BUSY)
      ;
    while (computePM(&pm10_val, PM10_SENSOR_PIN) == RET_BUSY)
      ;
    Serial.println("pm2.5_value:" + String(pm2_5_val));
    Serial.println("pm10_value:" + String(pm10_val));
    phase++;
  }
  break;
  
  case measure_temperature_humidity_st:
      humidity_val = dht.readHumidity();
      temperature_val = dht.readTemperature(false);
      Serial.println("humidity_value:" + String(humidity_val));
      Serial.println("temperature_value:" + String(temperature_val));
      phase++;
      break;

  case preparePacket_st:
    Serial.println("fase 2");

    // Prepara messaggio da POSTARE
    datiPOST = "valore_sensore=" + String(h2s_raw_value) + "&nox_val=" + String(nox_raw_value) + "&red_val=" + String(red_raw_value) + "&pm2.5_val=" + String(pm2_5_val) + "&pm10_val=" + String(pm10_val) + "&tmp_val=" + String(temperature_val) + "&hu_val=" + String(humidity_val)+ "&data_ora_creazione=" + dataOraCorrente + "&macAddress=" + macAddress;

    if (isWifiOk)
      phase = send_st;
    else
      phase = putInMem_st;
    break;

  case send_st:
  {
    Serial.println("fase 3");
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
    if (rispostaCodiceHTTP == 200)
      phase = goToSleep_st;
    else
      phase = putInMem_st;

    Serial.println(rispostaCodiceHTTP);
  }
  break;

  case putInMem_st:
    // memorizza datiPOST
    phase = goToSleep_st;
    break;

  case goToSleep_st:
  {
    static uint8_t retry = 0;
    Serial.println("fase 4");
    rtc.clearAlarm(1);
    delay(100);
    rtc.clearAlarm(2);
    retry++;
    if (retry > 5)
      ESP.deepSleep(SLEEP_TIME);
  }
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

int32_t computeAnalogMeanValue(uint8_t analogPin)
{
  uint16_t n_sensor_count = 0;
  int32_t mean_sensor_value = 0;
  int32_t delta_sensor_value = 0;

  for (int i = 0; i < N_SAMPLES; i++)
  {

    n_sensor_count++;
    delta_sensor_value = (int32_t)(analogReadMilliVolts(analogPin)) - mean_sensor_value;
    mean_sensor_value += (delta_sensor_value / n_sensor_count);
    // Serial.println("N : " + String(n_sensor_count) + " " + String(analogReadMilliVolts(analogPin)));
  }

  return mean_sensor_value;
}

uint8_t computePM(uint16_t *pm_value, uint8_t pin)
{
  static uint32_t timer;
  static uint8_t phase = 0;
  static uint8_t ret = RET_BUSY;

  switch (phase)
  {
  case 0:
    ret = RET_BUSY;
    phase++;
    break;
  case 1:
    if (digitalRead(pin) == LOW)
    {
      phase++;
    }
    break;
  case 2:

    if (digitalRead(pin) == HIGH)
    {
      resetTimer(&timer);
      phase++;
    }
    break;
  case 3:
    if ((getTime(timer)) > 2000)
    {
      *pm_value = 0xffff;
      ret = RET_ERR;
      phase = 0;
    }
    else if (digitalRead(pin) == LOW)
    {

      timer = getTime(timer);
      *pm_value = (uint16_t)(timer - 2); // ug_mm^2
      ret = RET_OK;
      phase = 0;
    }
    break;
  default:
    phase = 0;
    break;
  }

  return ret;
}