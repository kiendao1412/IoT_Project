#include <TinyGPSPlus.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

uint32_t lastSend = 0;
bool check4gConnected = false;     // true nếu 4G OK
bool lastUploadOK = false;     // true nếu lần gửi gần nhất OK
uint32_t lastUploadMs = 0;

static const int GPS_RXPin = 26, GPS_TXPin = 27;
static const uint32_t GpsBaud = 9600;
HardwareSerial gpsModule(2);
TinyGPSPlus gps;

static const int SIM_RXPin = 16, SIM_TXPin = 17;
static const uint32_t SimBaud = 115200;
HardwareSerial simModule(1);

// ########################################## BUTTON #####################################################################
#define BUTTON 4        
#define DEBOUNCE_MS 30        

bool buttonPressed()
{
  static uint32_t lastDebounceTime = 0;
  static bool lastState = HIGH;
  static bool buttonState = HIGH;
  bool reading = digitalRead(BUTTON);
  if (reading != lastState)
  {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS)
  {
    if (reading != buttonState)
    {
      buttonState = reading;
      if (buttonState == LOW)   
      {
        lastState = reading;
        return true;            
      }
    }
  }
  lastState = reading;
  return false;
}

// ########################################## GPS DATA #####################################################################
struct GpsData {
  bool valid;
  float lat, lon;
  // float spd, alt, hdop;
  int sats;
};
GpsData g = {false, 0.0f, 0.0f, -1};
// Múi giờ VN +7 
const int TZ_OFFSET_HOURS = 7;

void updateGps()
{
  while (gpsModule.available()) gps.encode(gpsModule.read());

  g.valid = gps.location.isValid();
  g.lat   = g.valid ? gps.location.lat() : 0.0f;
  g.lon   = g.valid ? gps.location.lng() : 0.0f;
  g.sats  = gps.satellites.isValid()? (int)gps.satellites.value() : -1;
  // g.spd   = gps.speed.isValid()     ? gps.speed.kmph()       : -1.0f;
  // g.alt   = gps.altitude.isValid()  ? gps.altitude.meters()  : -1.0f;
  // g.hdop  = gps.hdop.isValid()      ? gps.hdop.hdop()        : -1.0f;
}

// ######################################## 4G CONFIG #######################################################################
const char* APN = "v-internet";           // Nhà mạng Viettel     
// Đọc data Module Sim trả về
String readAll(uint32_t timeoutMs = 500){
  String s;
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs){
    while (simModule.available()){
      s += char(simModule.read());
    }
  }
    return s;
}
// Gửi lệnh AT qua UART và kiểm tra phản hồi
bool sendAT (const String& cmd, const char*ok = "OK", uint32_t timeoutMs = 1500){
  simModule.println(cmd);
  String r = readAll(timeoutMs);
  Serial.println(">> " + cmd);
  Serial.println("<< " + r);
  // Kiểm tra xem có phản hồi đúng không, mặc định ok = "OK"
  return (r.indexOf(ok) >= 0);
}

bool waitFor(const char* token, uint32_t timeoutMs=5000) {
  uint32_t t0 = millis();
  String r;
  while (millis() - t0 < timeoutMs) {
    r += readAll(200);
    if (r.indexOf(token) >= 0) return true;
  }
  return false;
}
// --------------------------- 4G Connect ---------------------------
bool ec600_connectInternet() {
  if (!sendAT("AT")) return false;
  sendAT("ATE0"); // tắt echo

  // SIM ready?
  if (!sendAT("AT+CPIN?", "READY", 2000)) return false;

  // Đăng ký mạng (CEREG?)
  // Chờ đến khi có mạng, lặp lại 20 lần
  bool registered = false;
  for (int i=0;i<20;i++){
    simModule.println("AT+CEREG?");
    String r = readAll(800);
    if (r.indexOf(",1")>=0 || r.indexOf(",5")>=0) { registered = true; break; }// Đăng ký thành công: 1:home, 5:roaming
    delay(500);
  }
  if (!registered) return false;
  // Set APN
  // AT+QICSGP=1,1,"APN","","",1
  String cmd = String("AT+QICSGP=1,1,\"") + APN + "\",\"\",\"\",1";
  if (!sendAT(cmd)) return false;

  // Activate PDP
  if (!sendAT("AT+QIACT=1", "OK", 8000)) return false;

  // Check got IP
  simModule.println("AT+QIACT?");
  String r = readAll(1500);
  return (r.indexOf(".") >= 0); // Có dấu chấm là có IP
}
// ########################################## ThingSpeak CONFIG #####################################################################
const char* TS_HOST = "api.thingspeak.com";
const int   TS_PORT = 80;
const char* THINGSPEAK_WRITE_KEY = "H28KJ7OWA9TYWG8M";
// ThingSpeak free: >=15s/lần
uint32_t SEND_INTERVAL_MS = 16000;

// field1=lat, field2=lon
String buildThingSpeakPath()
{
  String path = "/update?api_key=" + String(THINGSPEAK_WRITE_KEY);
  path += "&field1=" + String(g.valid ? g.lat : 0.0f, 6);
  path += "&field2=" + String(g.valid ? g.lon : 0.0f, 6);
  return path;
}
bool sendToThingSpeak_4G()
{
  String path = buildThingSpeakPath();

  // Open TCP socket id=0
  String openCmd = "AT+QIOPEN=1,0,\"TCP\",\"" + String(TS_HOST) + "\"," + String(TS_PORT) + ",0,1";
  simModule.println(openCmd);
  if (!waitFor("OK", 2000)) return false;

  if (!waitFor("+QIOPEN: 0,0", 10000)) {
    sendAT("AT+QICLOSE=0");
    return false;
  }

  // Build HTTP GET request
  String req;
  req += "GET " + path + " HTTP/1.1\r\n";
  req += "Host: " + String(TS_HOST) + "\r\n";
  req += "Connection: close\r\n\r\n";

  // Send request length
  simModule.println("AT+QISEND=0," + String(req.length()));
  if (!waitFor(">", 3000)) {
    sendAT("AT+QICLOSE=0");
    return false;
  }

  simModule.print(req);

  if (!waitFor("SEND OK", 8000)) {
    sendAT("AT+QICLOSE=0");
    return false;
  }

  // Read response (ThingSpeak body thường là số entry id)
  String resp = readAll(4000);
  Serial.println("ThingSpeak resp:");
  Serial.println(resp);

  sendAT("AT+QICLOSE=0");
  return true;
}

// ######################################## OLED #######################################################################
static uint32_t lastOled = 0;
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
// --------------------------- OLED helpers ---------------------------
static String twoDigits(int v) { return (v < 10) ? ("0" + String(v)) : String(v); }
// --------------------------- Hiển thị màn OLED ---------------------------
static void drawOLED()
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Dòng 1: State + Số vệ tinh + 4G
  display.setCursor(0, 0);
  display.print("GPS:");
  display.print(g.valid ? "OK " : "NO ");

  display.print("SAT:");
  if (g.sats >= 0) display.print(g.sats);
  else display.print("--");

  // 4G status 
  display.setCursor(96, 0);
  display.print("4G:");
  display.print(check4gConnected ? "OK" : "--");

  // Dòng 2-3: Lat-Vĩ độ + Lon-Kinh độ
  display.setCursor(0, 16);
  display.print("LAT:");
  display.print(g.valid ? String(g.lat, 6) : String("--------"));

  display.setCursor(0, 28);
  display.print("LON:");
  display.print(g.valid ? String(g.lon, 6) : String("--------"));

  // Dòng 4: Time (VN) + age-Tuổi dữ liệu tính từ dữ liệu GPS hợp lệ gần nhất(ms)
  display.setCursor(0, 48);
  if (gps.time.isValid())
  {
    int hh = gps.time.hour() + TZ_OFFSET_HOURS;
    if (hh >= 24) hh -= 24;

    display.print(twoDigits(hh)); display.print(":");
    display.print(twoDigits(gps.time.minute())); display.print(":");
    display.print(twoDigits(gps.time.second()));
  }
  else display.print("--:--:--");

  // Upload status 
  bool showUpload = (millis() - lastUploadMs < 5000);
  display.setCursor(72, 48);
  display.print("UP:");
  display.print(showUpload ? (lastUploadOK ? "OK" : "NO") : "--");

  display.display();
}

// ######################################## void setup #######################################################################
void setup()
{
  Serial.begin(115200);
  pinMode(BUTTON, INPUT_PULLUP);

// =============================== GPS + SIM ===============================
  gpsModule.begin(GpsBaud, SERIAL_8N1, GPS_RXPin, GPS_TXPin);
  simModule.begin(SimBaud, SERIAL_8N1, SIM_RXPin, SIM_TXPin);
  Serial.println("EC600N: connecting internet...");
  check4gConnected= ec600_connectInternet();
  Serial.println(check4gConnected ? "4G Internet OK" : "4G Internet FAIL");

// =============================== OLED ===============================
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
  {
    Serial.println("Screen init failed!");
    while (true) delay(100);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("GPS System starting...");
  display.display();
}

// ######################################## void loop #######################################################################
void loop()
{
  
  updateGps();
    // ===== MOCK GPS DATA (test trong nhà) =====

  // g.valid = true;
  // g.lat   = 21.0285;    // Hà Nội
  // g.lon   = 105.8542;
  // g.spd   = 0.0;
  // g.alt   = 10.0;
  // g.sats  = 8;
  // g.hdop  = 0.9;

  if (millis() - lastOled > 300) {
    lastOled = millis();
    drawOLED();
  }
  // Gửi định kỳ
  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();
    lastUploadOK = sendToThingSpeak_4G();  
    lastUploadMs = millis();              
    Serial.println(lastUploadOK ? "UPLOAD OK" : "UPLOAD FAIL");
  }
  if (millis() > 5000 && gps.charsProcessed() < 10)
    Serial.println(F("No GPS data received: check wiring"));
}