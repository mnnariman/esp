#include <HttpClient.h>
 #include "Extras.h"

 typedef struct {
   float reading;
   int timeStamp;
 } data;

typedef union{
    uint8_t addr;
    struct {
        uint8_t bit1 : 1; // lsb
        uint8_t bit2 : 1;
        uint8_t bit4 : 1;
        uint8_t bit8 : 1;
        uint8_t highNibble : 4;
    };
} AddressUnion;

AddressUnion a;
data d;
data allData[100];
int indx;

OneShot timer6AM(6,0); // timer that fires once per day at 6:00
OneShot timer7AM(7,0); // timer that fires once per day at 7:00
OneShot timer7Thirty(7,30); // timer that fires once at 7:30 AM
OneShot timer11_30PM(23,30);
Wait fiveSecs(5, SECONDS); // a hidden millis timer. Can be MILLIS, SECONDS, or MINUTES
Timer fillRateTimer(300000, fillRateTimerHandler);
double fillStartValue;

const int dataPin = D2; // data pin to read the A3212 Hall Effect Switches (connected to the common in/out pin on the CD4067)

const int chipInh0 = A2; // chip inhibit pins for the 3 CD4067 1x16 multiplexers. The chip is selected when the chip inhibit pin is LOW (inhibited when HIGH)
const int chipInh1 = A3;
const int chipInh2 = A4;

const int onesBit = D1; // address pin A on a CD4067
const int twosBit = D0; // address pin B
const int foursBit = A1; // address pin C
const int eightsBit = A0; // address pin D

int counter;
int timeFilled;
double gallonsDown;
double twentyThreeThirtyGallonsDown;
char valuesString[620];
bool shouldReset;
bool shouldMonitorFillRate;

// The first 25 Hall effect switches are 3/4" apart. The next 15 are 1" apart. The last 8 (lowest down in the tank) are 2" apart
// There are two places in the array below where the values are out of order (11&12 and 27&28) -- this is intentional to correct for wiring mistakes
float inchesDown[48] = {0.032, 0.75, 1.5, 2.25, 3, 3.75, 4.5, 5.25, 6, 6.75, 7.5, 9, 8.25, 9.75, 10.5, 11.25, 12, 12.75, 13.5, 14.25, 15, 15.75, 16.5, 17.25, 18, 19, 20, 22, 21, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 35, 37, 39, 41, 43, 45, 47, 49};

http_request_t request;
http_response_t response;
HttpClient http;
http_header_t headers[] = {
        { "Content-Type", "application/json" },
        { NULL, NULL }
    };

STARTUP(WiFi.selectAntenna(ANT_EXTERNAL));
SYSTEM_THREAD(ENABLED);


void setup() {
  waitFor(Particle.connected, 600000); // wait up to 10 minutes for Wi-Fi router to come back online after power failure
  Particle.function("getRSSI", getRSSI);
  Particle.function("reset", resetDevice);
  Particle.function("fillMonitor", fillMonitor);
    Particle.variable("gallonsDown", gallonsDown);
    Particle.variable("values", valuesString);
  Particle.variable("timeFilled", timeFilled);
  pinMode(D7, OUTPUT);
  digitalWrite(D7, HIGH);
  waitFor(Time.isValid, 10000);
  digitalWrite(D7, LOW);
  setZone(); // sets time zone to Pacific Daylight Time or Pacific Standard Time depending on the date
    request.hostname = "things.ubidots.com";
    request.port = 80;

    pinMode(dataPin, INPUT_PULLUP);

    pinMode(onesBit, OUTPUT);
    pinMode(twosBit, OUTPUT);
    pinMode(foursBit, OUTPUT);
    pinMode(eightsBit, OUTPUT);

    pinMode(chipInh0,OUTPUT);
    pinMode(chipInh1,OUTPUT);
    pinMode(chipInh2,OUTPUT);

  fiveSecs.begin(); // nothing here other than start = millis() for a millis-like timer object
  //Particle.publish("WellDry", "Well is running dry");
}


void loop() {

  if (timer11_30PM.fired()) twentyThreeThirtyGallonsDown = gallonsDown;

  if (shouldReset == true) {
      shouldReset = false;
      System.reset();
  }

  // Sync time with the cloud once a day at 7:00 AM
  if (timer7AM.fired()) {
      Particle.syncTime();
      waitFor(Time.isValid, 30000);
      setZone();
      for (int i=0; i<100; i++) {
           allData[i] = {0, -1};
       }
       indx = 0;
       valuesString[0] = 0; // reset valueString to empty string once a day
    }


    if (timer6AM.fired()) {
        if (allData[indx].reading > 0.04) {
            char sixAMValue[30];
            sprintf(sixAMValue,"Tank down by %.1f gallons", allData[indx].reading * 31.33 );
            Particle.publish("WaterTankFailure", sixAMValue, 60, PRIVATE); // triggers a webhook that sends a notification to my phone through Pushover
        }
    }

  if (timer7Thirty.fired()) {
    timeFilled = 0;
  }

    if (fiveSecs.isUp()) cycleAddressPins();
}


void cycleAddressPins() {
    float total = 0;
    counter = 0;
    for (a.addr=0; a.addr<47; a.addr++) { // 47 instead of 48 because the last sensor (lowest down in the tank) is non-functional and always reads LOW (as if a magnet was next to it)

    a.highNibble == 0 ? pinResetFast(chipInh0) : pinSetFast(chipInh0);
        a.highNibble == 1 ? pinResetFast(chipInh1) : pinSetFast(chipInh1);
        a.highNibble == 2 ? pinResetFast(chipInh2) : pinSetFast(chipInh2);
        delay(50);
        digitalWriteFast(onesBit, a.bit1);
        digitalWriteFast(twosBit, a.bit2);
        digitalWriteFast(foursBit, a.bit4);
        digitalWriteFast(eightsBit, a.bit8);
        delay(50);
        int magnetReading = digitalRead(dataPin);

        // interpolate between sensors if 2 are activated at the same time
        if (magnetReading == 0) {
            total += inchesDown[a.addr];
            counter ++;
        }
    }

  float lastValue = (indx == 0)? allData[0].reading : allData[indx - 1].reading;
    float level = (counter >0)? total/counter : lastValue;
    gallonsDown = level * -31.33; // there are 31.33 gallons of water per inch of height in my 8' diameter tank

  if ((Time.hour() < 7  || (Time.hour() > 22 && Time.minute() > 30))  &&  gallonsDown - twentyThreeThirtyGallonsDown > 48  &&  Particle.connected()) {
    Particle.publish("TankAlert", PRIVATE);
  }

    if (level != lastValue && indx < 99) {
    allData[indx].reading = level;
    allData[indx].timeStamp = Time.hour() * 60 + Time.minute();

    if (shouldMonitorFillRate == true) {
      shouldMonitorFillRate = false;
      fillStartValue = gallonsDown;
      fillRateTimer.start();
    }

    // get rid of repeat data if the last pair of values is the same as the pair before it
    if (indx > 2) {
      if (allData[indx].reading == allData[indx - 2].reading && allData[indx - 1].reading == allData[indx - 3].reading) {
        allData[indx] = {0, -1};
        allData[indx - 1] = {0, -1};
        indx -= 2;
      }
    }

    indx++;

    if ((Time.hour() > 21 || Time.hour() < 6) && level < 0.1 && timeFilled == 0) timeFilled = Time.hour() * 60 + Time.minute();

    valuesString[0] = 0;
    for (int i=0; i<indx; i++) {
      char s[15];
      sprintf(s, "%d-%.2f, ", allData[i].timeStamp, allData[i].reading);
      strcat(valuesString, s);
    }

        request.path = "/api/v1.6/variables/576f55be7625422b94879009/values?token=dqLW9lJbK9ilKhuhGAMHQxuCeqchrp";
        request.body = "{\"value\":" + String(gallonsDown) + "}";
        http.post(request, response, headers);
    }
}


int fillMonitor(String cmd) {
  if (cmd == "start") {
    shouldMonitorFillRate = true;
    return 1;
  }else {
    fillRateTimer.stop();
    return -1;
  }
}


void fillRateTimerHandler() {
  if (gallonsDown == fillStartValue && abs(gallonsDown + 23.5) > 1 ) { // value stayed the same and the tank is not full
    Particle.publish("WellDry", PRIVATE);
  }else if (abs(gallonsDown + 23.5) < 1) { // tank is full
    fillRateTimer.stop();
    Particle.publish("TankFilled", PRIVATE);
  }else if (gallonsDown < fillStartValue) { // tank level changed in under 5 minutes
    fillStartValue = gallonsDown;
    Particle.publish("FillingFast", PRIVATE);
  }
}


int resetDevice(String cmd) {
  shouldReset = true;
  return 1;
}



int getRSSI(String cmd) {
    return WiFi.RSSI();
}
