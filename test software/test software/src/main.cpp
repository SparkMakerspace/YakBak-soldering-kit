#include <Arduino.h>

#define SERIAL_EN true
#define PWM_FREQ 40000
#define SAMPLE_FREQ 8000

uint8_t analogOut = 10;
uint8_t sayButton = 8;
uint8_t playButton = 7;
uint8_t amplifierShutdown = 9;
uint8_t microphone = 0;
hw_timer_t * timer = NULL;
volatile SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

volatile uint32_t isrCounter = 0;
volatile uint32_t lastIsrAt = 0;

void ARDUINO_ISR_ATTR onTimer(){
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  isrCounter++;
  lastIsrAt = millis();
  portEXIT_CRITICAL_ISR(&timerMux);
  // Give a semaphore that we can check in the loop
  xSemaphoreGiveFromISR(timerSemaphore, NULL);
  // It is safe to use digitalRead/Write here if you want to toggle an output
}

void setup()
{
  // Serial first
  if (SERIAL_EN)
  {
    Serial.begin();
    delay(10000); // delay so we have time to connect
  }

  // Pinmodes
  pinMode(analogOut, OUTPUT);
  pinMode(sayButton, INPUT_PULLUP);
  pinMode(playButton, INPUT_PULLUP);
  pinMode(amplifierShutdown, OUTPUT);


  // turn off the amplifier
  digitalWrite(amplifierShutdown, HIGH);


  // Set the PWM frequency, get the actual PWM frequency back
  uint32_t clockFreq = sigmaDeltaSetup(analogOut, 0, PWM_FREQ);
  // Print out the frequency via serial
  if (clockFreq == 0)
  {
    (SERIAL_EN)? Serial.println("Could not set PWM clock frequency"):0;
  }
  else
  {
    (SERIAL_EN)? Serial.printf("PWM clock freqncy: %d\n", clockFreq):0;
  }
  // No output yet
  sigmaDeltaWrite(0, 0);


  // Setup timer at the sample rate
  timer = timerBegin(0,80,true);
  // attach an ISR
  timerAttachInterrupt(timer,&onTimer,true);
  // enable "alarms" to trigger the ISR and reload the timer

}

void loop()
{
  // put your main code here, to run repeatedly:
  // check for button push
  // if say is being held down, record mic value to SPIFFS every sample rate
  // if a high->low play transition occurs, read every sample in SPIFFS to the 
  // sigmaDeltaWrite function every sample rate
}