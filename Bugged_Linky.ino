#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUDP.h>
#include <MySQL_Connection.h>
#include <MySQL_Cursor.h>
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <BMP180I2C.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <Time.h>
#include <Timezone.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>

#define TCAADDR 0x70
#define DHTTYPE DHT22
#define LINKYPIN 3 
#define PLUVIOPIN 18
#define ANEMOPIN  19
#define LUMPIN 28
#define SCROFFPIN 36
#define SCRONPIN 37
#define TH2PIN 32 
#define TH3PIN 33 
#define TH4PIN 34 
#define BTN1PIN 41
#define BTN2PIN 44
#define LEDBLANCHEPIN 42
#define LEDJAUNEPIN 40
#define LEDROUGEPIN 45
#define LEDBLEUEPIN 35
#define WINDDIRPIN A1

unsigned long msAnemo = 0;
unsigned long msPluvio = 0;
unsigned long msMain = 0;

const unsigned long intervUpdate = 60000;
const long intervWindSpeed = 3600000;
const int intervLinky = 3000;
const long intervAffTime = 1000;
const long intervNTP = 86400000; // 24h
const float incPluie = 0.2794;
unsigned long lastms = 0;
unsigned long lastmsAnemo = 0;
unsigned long lastmsNTP = 0;
unsigned long lastmsPluvio = 0;
unsigned long lastmsNtp = 0;
unsigned long lastmsAffTime = 0;
int niveauPuits;
float windDir;
float windSpeed = 0.0;
float windGust = 0.0;
unsigned long msAnemoMin;
unsigned long firstmsAnemoImp;
unsigned long lastmsAnemoImp;
int anemoNbImps=0;
volatile int pluieInc = 0;
float t0, h0, t1, h1, t2, h2, t3, h3, t4, h4, pr_abs, pr_rel, pluie;
float cumulPluie = 0.0;
long puissA;
unsigned long firstmsImpLinky=0;
volatile unsigned long lastmsImpLinky=0;
volatile int linkyNbImps=-1;
int lidarStrength; //signal strength of LiDAR
float lidarTemp;
int check; //save check value
int i, j;
time_t timestampLocal;
int uart_r[9]; //save data measured by LiDAR
int uart_w[6];
const int HEADER = 0x59; //frame header of data package
const int HEADER_W = 0x5A;
const int ID_SAMPLE_FREQ = 0x03;
const int ID_DIST_LIMIT = 0x3A;
const int windDirValsOffset = 7;
const int windDirsRaw[] = {64, 82, 92, 125, 184, 245, 288, 406, 462, 601, 632, 705, 788, 829, 888, 946};
const int windDirs[] = {1125, 675, 900, 1575, 1350, 2025, 1800, 225, 450, 2475, 2250, 3375, 0, 2925, 3150, 2700};
int lastAnemoState = HIGH;
int lastLum = LOW;
int lastBtn1 = HIGH;
int lastBtn2 = HIGH;
int scrOn = HIGH;
int lastScrOn = HIGH;
int scrOff = HIGH;
int lastScrOff = HIGH;

IPAddress ethIP(192, 168, 1, 49);
IPAddress ethGateway(192, 168, 1, 1);
IPAddress ethDNS(1, 1, 1, 1);
IPAddress ethSubnet(255, 255, 255, 0);
byte ethMac[] = {0xA8, 0x61, 0x0A, 0xAE, 0x73, 0x8C};
EthernetClient ethClient;
LiquidCrystal_I2C lcd(0x27, 20, 4);
Adafruit_SHT31 th0;
Adafruit_SHT31 th1;
BMP180I2C bmp180(0x77);
DHT th2(TH2PIN, DHTTYPE);
DHT th3(TH3PIN, DHTTYPE);
DHT th4(TH4PIN, DHTTYPE);

byte char00[8] = {
  0b01010,
  0b10101,
  0b10101,
  0b10101,
  0b10101,
  0b10101,
  0b10101,
  0b01010
               };

MySQL_Connection sqlConn((EthernetClient *)&ethClient);

ThreeWire threeWire(24, 22, 26); // RST DAT CLK
RtcDS1302<ThreeWire> Rtc(threeWire);

unsigned int localUdpPort = 8888;       // local port to listen for UDP packets
const char timeServer[] = "fr.pool.ntp.org"; // NTP server
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte ntpPacketBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
EthernetUDP Udp;

TimeChangeRule myDST = {"EDT", Fourth, Sun, Mar, 3, 120};    // Daylight time = UTC - 4 hours
TimeChangeRule mySTD = {"EST", Fourth, Sun, Nov, 3, 60};     // Standard time = UTC - 5 hours
Timezone myTZ(myDST, mySTD);
TimeChangeRule *tcr;

IPAddress mysqlIP(192, 168, 1, 39); // IP of the MySQL *server* here
char mysqlLogin[] = "#############";
char mysqlPass[] = "###############################";

const int HTTP_PORT = 80;
const String HTTP_METHOD = "GET"; // or POST
const char HOST_NAME[] = "192.168.1.39";

void setLedRouge(bool level)
{
  digitalWrite(LEDROUGEPIN, level);
}

void setLidarSampleFrequency()
{
  //       5A len id val[3] chk
  // 10Hz [5A 06 03 0A 00 00]
  uart_w[0] = HEADER_W;
  uart_w[1] = 6;
  uart_w[2] = ID_SAMPLE_FREQ;
  uart_w[3] = 0x1; // 1 Hz
  uart_w[4] = 0x0;
  uart_w[5] = 0;
  for (j = 0; j < 5; j++)
    uart_w[5] += uart_w[j];
  for (j = 0; j < 6; j++)
    Serial2.write(uart_w[j]);
  delay(200);
}

void setEthernet()
{
  Ethernet.init(10); // CS pin
  Ethernet.begin(ethMac, ethIP, ethDNS, ethGateway, ethSubnet);
  delay(200);
  if (Ethernet.hardwareStatus() == EthernetNoHardware)
  {
    setLedRouge(HIGH);
    Serial.println("ERREUR: Ethernet shield manquant");
    while (true) //no point running without Ethernet hardware
    {
      setLedRouge(HIGH);
      delay(500);
      setLedRouge(LOW);
      delay(500);
    }
  }
  delay(200);
  if (Ethernet.linkStatus() == LinkOFF)
  {
    setLedRouge(HIGH);
    Serial.println("ERREUR: c??ble ethernet non connect??");
  }
  if (Ethernet.linkStatus() != LinkON)
  {
    setLedRouge(HIGH);
    while (Ethernet.linkStatus() != LinkON)
      delay(100);
  }
  setLedRouge(LOW);
  Serial.println("Ethernet connect??");
}

void setMysql()
{
  Serial.println("MySQL tentative de connexion...");
  while (!sqlConn.connect(mysqlIP, 3306, mysqlLogin, mysqlPass))
    delay(1000);
  delay(200);
}

void setTH0()
{
  th0 = Adafruit_SHT31();
  if (! th0.begin(0x44))
  {
    setLedRouge(HIGH);
    Serial.println("th0 muet");
  }
  else
    Serial.println("th0 OK");
}

void setTH1()
{
  th1 = Adafruit_SHT31();
  if (! th1.begin(0x45))
  {
    setLedRouge(HIGH);
    Serial.println("th1 muet");
  }
  else
    Serial.println("th1 OK");
}

void setThermoHygro()
{
  Serial.println("setThermoHygro()");
  setTH0();
  setTH1();
  th2.begin();
  th3.begin();
  th4.begin();
}

void setAnemometre()
{
  pinMode(A2, INPUT);
}

void setPluviometre()
{
  pinMode(A0, INPUT);
}

void setPression()
{
  if (!bmp180.begin())
  {
    Serial.println("BMP180 injoignable.");
    while (1)
    {
      setLedRouge(HIGH);
      delay(500);
      setLedRouge(LOW);
      delay(500);
    }
  }
  Serial.println("BMP180 OK");
}

int getNiveauPuits()
{
  int hauteur = -1;
  bool bleme = true;
  while (bleme)
  {
    if (Serial2.available())
    {
      if (Serial2.read() == HEADER)
      {
        uart_r[0] = HEADER;
        if (Serial2.read() == HEADER)
        {
          uart_r[1] = HEADER;
          for (i = 2; i < 9; i++)
            uart_r[i] = Serial2.read();
          check = 0;
          for (i = 0; i <= 7; i++)
            check += uart_r[i];
          if (uart_r[8] == (check & 0xff))
          {
            hauteur = 294 - (uart_r[2] + uart_r[3] * 256); //calculate distance value
            lidarStrength = uart_r[4] + uart_r[5] * 256; //calculate signal strength value
            lidarTemp = (uart_r[6] + uart_r[7] * 256) / 8 - 256; //calculate chip temprature
            bleme = false;
          }
          else
            Serial.println("ERREUR: checksum Lidar");
        }
        else
          Serial.println("ERREUR: Serial2 (1)");
      }
      else
        Serial.println("ERREUR: Serial2");
    }
    else
      Serial.println("ERREUR: Serial2 unavailable");

    if (bleme)
    {
      setLedRouge(HIGH);
      Serial2.end();
      delay(100);
      Serial2.begin(115200);
      delay(250);
    }
  }
  return (hauteur);
}

void setNpHTTP(long np)
{
  while (!ethClient.connect(HOST_NAME, HTTP_PORT))
  {
    setLedRouge(HIGH);
    delay(500);
    Serial.print(".");
  }
  setLedRouge(LOW);
  // Make a HTTP request:
  ethClient.print("GET /caverne/setNiveauPuits.php?np=" + String(np) + " HTTP/1.0\r\n");
  // empty line -> end of header
  ethClient.print("\r\n");

  //ethClient.stop();
  Serial.print(String(np));
  Serial.println(" -> HTTP");
}

float round1(float val)
{
  return (round(val * 10) / 10);
}

void setDataHTTP()
{
  while (!ethClient.connect(HOST_NAME, HTTP_PORT))
  {
    setLedRouge(HIGH);
    delay(500);
    Serial.print(".");
  }
  setLedRouge(LOW);

  int windDirInt = round(windDir);
  int windSpeedInt = round(windSpeed * 10);
  int windGustInt = round(windGust * 10);
  int pluieInt = round(pluie * 100);
  int t0Int = round(t0 * 10);
  int h0Int = round(h0 * 10);
  int t1Int = round(t1 * 10);
  int h1Int = round(h1 * 10);
  int t2Int = round(t2 * 10);
  int h2Int = round(h2 * 10);
  int t3Int = round(t3 * 10);
  int h3Int = round(h3 * 10);
  int t4Int = round(t4 * 10);
  int h4Int = round(h4 * 10);
  //long paInt = round(pr_abs * 100);
  //long prInt = round(pr_rel * 100);
  int lidarTempInt = round(lidarTemp * 10);

  //String request = "/caverne/setDataMeteo.php?np=" + String(niveauPuits) + "&wd=" + String(windDirInt) + "&ws=" + String(windSpeedInt) + "&wg=" + String(windGustInt) + "&pl=" + String(pluieInt) + "&t0=" + String(t0Int) + "&h0=" + String(h0Int) + "&t1=" + String(t1Int) + "&h1=" + String(h1Int) + "&pa=" + String(paInt) + "&pr=" + String(prInt) + "&t2=" + String(t2Int) + "&h2=" + String(h2Int) + "&t3=" + String(t3Int) + "&h3=" + String(h3Int) + "&t4=" + String(t4Int) + "&h4=" + String(h4Int) + "&lidstr=" + String(lidarStrength) + "&lidt=" + String(lidarTempInt);
  String request = "/caverne/setDataMeteo.php?np=" + String(niveauPuits) + "&wd=" + String(windDirInt) + "&ws=" + String(windSpeedInt) + "&wg=" + String(windGustInt) + "&pl=" + String(pluieInt) + "&t0=" + String(t0Int) + "&h0=" + String(h0Int) + "&t1=" + String(t1Int) + "&h1=" + String(h1Int) + "&t2=" + String(t2Int) + "&h2=" + String(h2Int) + "&t3=" + String(t3Int) + "&h3=" + String(h3Int) + "&t4=" + String(t4Int) + "&h4=" + String(h4Int) + "&lidstr=" + String(lidarStrength) + "&lidt=" + String(lidarTempInt);
  Serial.print("http://192.168.1.39");
  Serial.print(request);
  request="GET "+request + " HTTP/1.0\r\n";
  ethClient.print(request);
  ethClient.print("\r\n");  // empty line -> end of header
}

void setPuissAHTTP()
{
  while (!ethClient.connect(HOST_NAME, HTTP_PORT))
  {
    setLedRouge(HIGH);
    delay(500);
    Serial.print(".");
  }
  setLedRouge(LOW);

  String request = "GET /caverne/setLinkyPuissanceActive.php?pa=" + String(puissA) + " HTTP/1.0\r\n";
  //Serial.print(request);
  ethClient.print(request);
  ethClient.print("\r\n");  // empty line -> end of header

}
int getWindDir()
{
  int windDirRaw = analogRead(WINDDIRPIN);
  /*
    Serial.print("windDirRaw: ");
    Serial.println(windDirRaw);
  */
  int i = 15;
  while ((windDirRaw < windDirsRaw[i]) && (i != 0))
    i--;
  return (windDirs[i]);
}

float getPression()
{
  float p;
  while (!bmp180.measurePressure())
  {
    Serial.println("\nBM180: Mesure impossible. Capteur occup???\n");
    delay(500);
  }

  //wait for the measurement to finish. proceed as soon as hasValue() returned true.
  do
  {
    delay(100); // 100
  } while (!bmp180.hasValue());
  p = bmp180.getPressure() / 100;
  return (p);
}

void initLCD()
{
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, char00);
  lcd.clear();
  lcd.home();
  /*
    lcd.setCursor(8, 1);
    lcd.print("...");
  */
}


int lcdXOffsetInt(int v, int nc)
{
  int d = 1;
  int i;
  for (i = nc - 1; i > 0; i--)
    if (v < pow(10, i))
      d++;
  return (d);
}

int lcdXOffsetFloat(float v, int nc)
{
  int d = 1;
  int i;
  for (i = nc - 1; i > 0; i--)
    if (v < pow(10, i))
      d++;
  return (d);
}

void affValLCD(float v, int x, int y, unsigned int nc, unsigned int nd, bool neg, unsigned int lngMax)
{
  int m = pow(10, nd);
  float a = float(round(v * m));
  String r0 = String(round(floor(a / m)));
  String r1 = String(round(round(a) % m));
  while (r0.length() < (nc))
    r0 = " " + r0;
  if (neg && (v >= 0))
    r0 = " " + r0;
  while (r1.length() < (nd))
    r1 += "0";
  String r = r0 + "." + r1;
  while (r.length() < lngMax)
    r += " ";
  lcd.setCursor(x, y);
  lcd.print(r);
}

void affValLCDInt(int v, int x, int y, unsigned int nc, bool neg, unsigned int lngMax, unsigned int ncMin)
{
  String r = String(v);
  while (r.length() < ncMin)
    r = "0" + r;
  while (r.length() < nc)
    r = " " + r;
  if (neg && (v >= 0))
    r = " " + r;
  while (r.length() < lngMax)
    r += " ";
  lcd.setCursor(x, y);
  lcd.print(r);
}

void affLCDPuissA()
{
  affValLCDInt(puissA, 14, 2, 4, false, 4, 1);
}

void affPluieLCD()
{
  affValLCD(cumulPluie, 14, 1, 4, 1, false, 6);
}

void affWindGustLCD()
{
  affValLCD(windGust, 8, 2, 3, 1, false, 5);
}

void affTimeLCD()
{
  unsigned long msTime = millis();
  lastmsAffTime = msTime;
  //unsigned long timestamp=timestampLocal+round((msTime-lastmsNTP)/1000);
  unsigned long timestamp = Rtc.GetDateTime();
  int heure = (timestamp % 86400L) / 3600;
  int minutes = (timestamp % 3600) / 60;
  int secondes = timestamp % 60;
  affValLCDInt(heure, 12, 3, 2, false, 2, 2);
  lcd.setCursor(14, 3);
  lcd.print(":");
  affValLCDInt(minutes, 15, 3, 2, false, 2, 2);
  lcd.setCursor(17, 3);
  lcd.print(":");
  affValLCDInt(secondes, 18, 3, 2, false, 2, 2);

  //lcd.setCursor(12,3);
  //RtcDateTime now=Rtc.GetDateTime();
  //lcd.print(now);
  //Serial.println(now);
}
void deleteCharLCD(int x, int y)
{
  lcd.setCursor(x, y);
  lcd.print(" ");
}

void affLCD()
{
  /*
    lcd.clear();
    lcd.home();
  */
  affValLCD(t1, 0, 0, 2, 1, false, 4); affValLCDInt(round(h1), 5, 0, 2, false, 3, 1); affValLCD(t0, 8, 0, 2, 1, true, 5); affValLCDInt(round(h0), 14, 0, 2, false, 3, 1); affValLCDInt(niveauPuits, 17, 0, 3, false, 3, 1);
  affValLCD(t3, 0, 1, 2, 1, false, 4); affValLCDInt(round(h3), 5, 1, 2, false, 3, 1); affValLCD(windSpeed, 8, 1, 3, 1, false, 5); affPluieLCD();
  affValLCD(t2, 0, 2, 2, 1, false, 4); affValLCDInt(round(h2), 5, 2, 2, false, 3, 1); affWindGustLCD(); affLCDPuissA();
  affValLCD(t4, 0, 3, 2, 1, false, 4); affValLCDInt(round(h4), 5, 3, 2, false, 3, 1); affValLCDInt(round(windDir / 10), 8, 3, 3, false, 3, 1); affTimeLCD();
}

void setScrBacklight(bool force)
{
  bool change = false;
  scrOn = digitalRead(SCRONPIN);
  if ((scrOn != lastScrOn) || force)
  {
    lastScrOn = scrOn;
    change = true;
    if (scrOn == LOW)
      lcd.backlight();
  }

  if (scrOn == HIGH)
  {
    scrOff = digitalRead(SCROFFPIN);
    if ((scrOff != lastScrOff) || force)
    {
      change = true;
      lastScrOff = scrOff;
      if (scrOff == LOW)
        lcd.noBacklight();
    }

    if (scrOff == HIGH)
    {
      int lum = digitalRead(LUMPIN);
      if ((lum != lastLum) || change)
      {
        lastLum = lum;
        if (lum == HIGH)
          lcd.noBacklight();
        else
          lcd.backlight();
      }
    }
  }
}

/*
void setPuissASQL()
{
  MySQL_Cursor *cur_mem = new MySQL_Cursor(&sqlConn);
  char req[round(70 + ceil(log10(puissA)))];
  sprintf(req, "INSERT INTO caverne.linky (dh,pa) VALUES ((SELECT UNIX_TIMESTAMP()),%d)", puissA);
  //Serial.println(req);
  cur_mem->execute(req);
  delete cur_mem;
}
*/

// send an NTP request to the time server at the given address
void sendNTPpacket(const char * address) {
  // set all bytes in the buffer to 0
  memset(ntpPacketBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  ntpPacketBuffer[0] = 0b11100011;   // LI, Version, Mode
  ntpPacketBuffer[1] = 0;     // Stratum, or type of clock
  ntpPacketBuffer[2] = 6;     // Polling Interval
  ntpPacketBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  ntpPacketBuffer[12]  = 49;
  ntpPacketBuffer[13]  = 0x4E;
  ntpPacketBuffer[14]  = 49;
  ntpPacketBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); // NTP requests are to port 123
  Udp.write(ntpPacketBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void setDS1302Time()
{
  if (Rtc.GetIsWriteProtected())
  {
    Serial.println("RTC was write protected, enabling writing now");
    Rtc.SetIsWriteProtected(false);
  }
  if (!Rtc.GetIsRunning())
  {
    Serial.println("RTC was not actively running, starting now");
    Rtc.SetIsRunning(true);
  }
  //RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  RtcDateTime ntpTime = RtcDateTime(timestampLocal);
  Rtc.SetDateTime(ntpTime);
}

void getNTPTime()
{
  sendNTPpacket(timeServer); // send an NTP packet to a time server

  // wait to see if a reply is available
  delay(1000);
  if (Udp.parsePacket())
  {
    lastmsNTP = millis();
    // We've received a packet, read the data from it
    Udp.read(ntpPacketBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    // the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, extract the two words:

    unsigned long highWord = word(ntpPacketBuffer[40], ntpPacketBuffer[41]);
    unsigned long lowWord = word(ntpPacketBuffer[42], ntpPacketBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    // now convert NTP time into everyday time:
    const unsigned long seventyYears = 2208988800UL; // Unix time starts on Jan 1 1970. In seconds, that's 2208988800
    time_t timestampUTC = secsSince1900 - seventyYears; // subtract seventy years
    timestampLocal = myTZ.toLocal(timestampUTC, &tcr);
    setDS1302Time();
  }
  Ethernet.maintain();
}

void initDS1302()
{
  Rtc.Begin();
  if (!Rtc.GetIsRunning())
  {
    Serial.println("RTC was not actively running, starting now");
    Rtc.SetIsRunning(true);
  }
}

void gererPluvio()
{
  pluieInc++;
}

void gererLinky()
{
  lastmsImpLinky=millis();
  linkyNbImps++;
}

void setup()
{
  pinMode(LEDROUGEPIN, OUTPUT);
  pinMode(LEDJAUNEPIN, OUTPUT);
  pinMode(LEDBLANCHEPIN, OUTPUT);
  pinMode(TH2PIN, INPUT);
  pinMode(TH3PIN, INPUT);
  pinMode(TH4PIN, INPUT);
  pinMode(LUMPIN, INPUT);
  pinMode(BTN1PIN, INPUT_PULLUP);
  pinMode(BTN2PIN, INPUT_PULLUP);
  pinMode(LINKYPIN, INPUT_PULLUP);
  pinMode(SCRONPIN, INPUT_PULLUP);
  pinMode(SCROFFPIN, INPUT_PULLUP);

  pinMode(2, INPUT_PULLUP);
  pinMode(4, INPUT_PULLUP);
  pinMode(5, INPUT_PULLUP);
  pinMode(6, INPUT_PULLUP);
  pinMode(7, INPUT_PULLUP);
  pinMode(8, INPUT_PULLUP);
  pinMode(9, INPUT_PULLUP);
  pinMode(10, INPUT_PULLUP);
  pinMode(11, INPUT_PULLUP);
  pinMode(12, INPUT_PULLUP);
  pinMode(13, INPUT_PULLUP);
  

  setLedRouge(HIGH);
  digitalWrite(LEDJAUNEPIN, HIGH);

  Serial.begin(115200); //set bit rate of serial port connecting Arduino with computer
  Serial2.begin(115200); //set bit rate of serial port connecting LiDAR with Arduino
  setEthernet();
  Udp.begin(localUdpPort);
  initLCD();
  initDS1302();
  //setMysql();
  delay(300);
  setLidarSampleFrequency();
  setThermoHygro();
  setAnemometre();
  setPluviometre();
  //setPression();
  setScrBacklight(true);
  setLedRouge(LOW);
  getNTPTime();

  attachInterrupt(digitalPinToInterrupt(PLUVIOPIN),gererPluvio,FALLING);
  attachInterrupt(digitalPinToInterrupt(LINKYPIN),gererLinky,FALLING);   
  Serial.println("Advienne que pourra");
}

void loop()
{
  if (Ethernet.linkStatus() != LinkON)
  {
    setLedRouge(HIGH);
    setEthernet();
  }

  long ms = millis();

  setScrBacklight(false);

  if ((ms - lastmsNTP) > intervNTP)
    getNTPTime();

  if ((ms - lastmsAffTime) >= intervAffTime)
    affTimeLCD();

  int btn1 = digitalRead(BTN1PIN);
  if (btn1 != lastBtn1)
  {
    if (btn1 == LOW)
    {
      cumulPluie = 0.0;
      affPluieLCD();
    }
    lastBtn1 = btn1;
  }

    int btn2 = digitalRead(BTN2PIN);
    if (btn2 != lastBtn2)
    {
      if (btn2 == LOW)
      {
        Serial.println("Bouton 2");
        lcd.clear();
        affLCD();
      }
    lastBtn2 = btn2;
    }

  digitalWrite(LEDJAUNEPIN,!digitalRead(LINKYPIN));
  digitalWrite(LEDBLANCHEPIN, digitalRead(ANEMOPIN));

// gestion Linky ??????????????????????????????????????????????????????????????????????????????????????????????????????

  noInterrupts();
  int linkyNbImpsTemp = linkyNbImps;
  unsigned long lastmsImpLinkyTemp = lastmsImpLinky;
  interrupts();
  if (linkyNbImpsTemp>0)
  {
    if (lastmsImpLinkyTemp!=0)
    {
      if (firstmsImpLinky==0)
        firstmsImpLinky=lastmsImpLinkyTemp;
      else  
      {
        unsigned long intervmsLinky=lastmsImpLinkyTemp-firstmsImpLinky;
        if (intervmsLinky>=intervLinky)
        {
          noInterrupts();
          linkyNbImps=0;
          interrupts();
          puissA = (float)linkyNbImpsTemp * 3600000.0 / float(intervmsLinky);
          firstmsImpLinky=lastmsImpLinkyTemp;        
          Serial.print(puissA);
          Serial.print("W");
          Serial.print("   ");
          Serial.print(linkyNbImpsTemp);
          Serial.print(" / ");
          Serial.print(intervmsLinky);
          Serial.println(" ms");
          
          affLCDPuissA();
          //setPuissASQL();
          setPuissAHTTP();
        }  
      }  
    }   
  }
  
 
  // gestion An??mom??tre ??????????????????????????????????????????????????????????????????????????????????????????????????????
  int anemoState = digitalRead(ANEMOPIN);
  if (lastAnemoState && (!anemoState))
  {
    unsigned long intervmsAnemo = ms - lastmsAnemoImp;
    if ((anemoNbImps==0)||(intervmsAnemo<msAnemoMin))
      msAnemoMin=intervmsAnemo;
    anemoNbImps++;
    lastmsAnemoImp=ms;
  }
  lastAnemoState = anemoState;
/*
  long intervPl = ms - lastmsPluvio;
  int pluvioState = digitalRead(PLUVIOPIN);
  if ((pluvioState == LOW) && (lastPluvioState == HIGH) && (intervPl != 0))
    pluieInc++;
  lastPluvioState = pluvioState;
*/
  if ((ms - lastms) >= intervUpdate) // transmission valeurs
  {
    lastms = ms;

    setLedRouge(LOW);
    niveauPuits = getNiveauPuits();
    Serial.print("np:");
    Serial.print(niveauPuits);
    Serial.print(" str:");
    Serial.print(lidarStrength);
    Serial.print(" t:");
    Serial.print(lidarTemp);

    /*
    pluie = incPluie * pluieInc;
    cumulPluie += pluie;
    */
    // gestion pluviom??tre ??????????????????????????????????????????????????????????????????????????????????????????????????????
    noInterrupts();
    pluie = incPluie * pluieInc;
      pluieInc=0;
    cumulPluie += pluie;
    interrupts();
        
    t0 = th0.readTemperature();
    Serial.print(" t0:");
    Serial.print(t0);

    h0 = th0.readHumidity();
    Serial.print(" h0:");
    Serial.print(h0);

    t1 = th1.readTemperature();
    Serial.print(" t1:");
    Serial.print(t1);

    h1 = th1.readHumidity();
    Serial.print(" h1:");
    Serial.print(h1);

    t2 = th2.readTemperature();
    Serial.print(" t2:");
    Serial.print(t2);

    h2 = th2.readHumidity();
    Serial.print(" h2:");
    Serial.print(h2);

    t3 = th3.readTemperature();
    Serial.print(" t3:");
    Serial.print(t3);

    h3 = th3.readHumidity();
    Serial.print(" h3:");
    Serial.print(h3);

    t4 = th4.readTemperature();
    Serial.print(" t4:");
    Serial.print(t4);

    h4 = th4.readHumidity();
    Serial.print(" h4:");
    Serial.print(h4);

    /*
    pr_abs = getPression();
    pr_rel = pr_abs + 122, 06;
    Serial.print(" pr_abs:");
    Serial.print(pr_abs);
    Serial.print(" pr_rel:");
    Serial.print(pr_rel);
    */

    if (anemoNbImps == 0)
    {
      windSpeed = 0.0;
      windGust = 0.0;
    }
    else
    {
      windSpeed = 2400 * float(anemoNbImps) / float(lastmsAnemoImp - firstmsAnemoImp);
      windGust = 2400 / float(msAnemoMin);
      firstmsAnemoImp=lastmsAnemoImp;
      anemoNbImps=0;
    }
    Serial.print(" ws:");
    Serial.print(windSpeed);
    Serial.print(" wg:");
    Serial.print(windGust);
    windDir = getWindDir();
    Serial.print(" wd:");
    Serial.print(windDir / 10);
    Serial.print(" pm:");
    Serial.println(pluie);

    affLCD();
    setDataHTTP();

    pluieInc = 0.0;
  }
}
