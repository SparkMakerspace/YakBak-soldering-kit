#include <Arduino.h>
#include <Preferences.h>

// Tweak these as desired
#define SERIAL_EN true    // Disable when we're ready for "production"
#define PWM_FREQ 40000    // This isn't exactly what it'll be, but that's okay.
#define SAMPLE_FREQ 8000  // Input and output samples/second
#define CLOCK_DIVIDER 80  // the APB clock should be running at 80MHz. This clock is used for the timer for the sample interrupt

// don't change these ones
#define COUNT_PER_SAMPLE APB_CLK_FREQ/CLOCK_DIVIDER/SAMPLE_FREQ // Just a pre-processor calculation to simplify runtime

uint32_t countPerSample = COUNT_PER_SAMPLE;
uint8_t analogOut = 10;
uint8_t sayButton = 8;
uint8_t playButton = 7;
uint8_t amplifierShutdown = 9;
uint8_t microphone = 0;
hw_timer_t * timer = NULL;
volatile SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// used for button state and edge detection
boolean sayPressed = false;
boolean wasSayPressed = false;
boolean sayJustPressed = false;
boolean playPressed = false;
boolean wasPlayPressed = false;
boolean playJustPressed = false;

void ARDUINO_ISR_ATTR onTimer(){
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  // TODO: if say was JUST pressed, clear SPIFFS first
  if (sayJustPressed) {
    // TODO: clear the SPIFFS file
  }
  // TODO: if say button is pressed, read ADC and write to end of SPIFFS
  portEXIT_CRITICAL_ISR(&timerMux);
  // Give a semaphore that we can check in the loop
  xSemaphoreGiveFromISR(timerSemaphore, NULL);
}

void setup()
{
  // Serial first
  if (SERIAL_EN)
  {
    Serial.begin();
    delay(5000); // delay so we have time to connect
  }

  // Pinmodes
  pinMode(analogOut, OUTPUT);
  pinMode(sayButton, INPUT_PULLUP);
  pinMode(playButton, INPUT_PULLUP);
  pinMode(amplifierShutdown, OUTPUT);

  (SERIAL_EN)? Serial.printf("timer count per interrupt firing: %d\n", countPerSample):0;

  // turn off the amplifier
  digitalWrite(amplifierShutdown, HIGH);

  Preferences preferences = Preferences();
  preferences.begin("nvs");
  (SERIAL_EN)? Serial.printf("Free entries in preferences space: %d\n", preferences.freeEntries()):0;

  // Create semaphore to inform us when the timer has fired
  timerSemaphore = xSemaphoreCreateBinary();

  // Set the PWM frequency, get the actual PWM frequency back
  uint32_t clockFreq = sigmaDeltaSetup(analogOut, 0, PWM_FREQ);
  // Print out the frequency via serial
  if (clockFreq == 0)
  {
    (SERIAL_EN)? Serial.println("Could not set PWM clock frequency"):0;
  }
  else
  {
    (SERIAL_EN)? Serial.printf("PWM clock frequency: %d\n", clockFreq):0;
  }
  // No output yet
  sigmaDeltaWrite(0, 0);

  // Setup timer at the sample rate
  timer = timerBegin(0, CLOCK_DIVIDER, true);
  // attach an ISR
  timerAttachInterrupt(timer, &onTimer, true);  
  // Set alarm to call onTimer function every second (value in microseconds).
  // Repeat the alarm (third parameter)
  timerAlarmWrite(timer, countPerSample, true);
  // Start an alarm to enable interrupts
  timerAlarmEnable(timer);
}

void loop()
{
  // put your main code here, to run repeatedly:
  if (xSemaphoreTake(timerSemaphore, 0) == pdTRUE){}
  // TODO: check for button push
  portENTER_CRITICAL(&timerMux);
    wasSayPressed = sayPressed;
    sayPressed = !digitalRead(sayButton);
    sayJustPressed = !wasSayPressed && sayPressed;
    wasPlayPressed = playPressed;
    playPressed = !digitalRead(playButton);
    sayJustPressed = !wasPlayPressed && playPressed;
  portEXIT_CRITICAL(&timerMux);
  // TODO: if say is being held down, record mic value to SPIFFS every sample rate
  // TODO: if a high->low play transition occurs, read every sample in SPIFFS to the 
  //       sigmaDeltaWrite function every sample rate
}