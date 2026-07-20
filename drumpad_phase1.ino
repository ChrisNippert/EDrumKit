#include "USB.h"
#include "USBMIDI.h"

const int LED_PIN 48; // LED strip connected to pin 48 on my dev board

// Multiplexer pins
const int SIG_PIN = 4;
const int S0_PIN = 5;
const int S1_PIN = 6;
const int S2_PIN = 7;
const int S3_PIN = 8;

// Drum Sensitivity
const int THRESHOLD = 50;
const int MAX_SENSE = 3200;

const int NOTE_DURATION_MS = 30;

USBMIDI MIDI;

// For selecting the multiplexer, 0-15
void selectChannel(int ch) {
  digitalWrite(S0_PIN, ch & 1);
  digitalWrite(S1_PIN, ch & 2);
  digitalWrite(S2_PIN, ch & 4);
  digitalWrite(S3_PIN, ch & 8);
}

void hitColor(int velocity) {
  float t = constrain((velocity - 20) / 107.0, 0.0, 1.0);

  int r, g, b;
  if (t < 0.33) {
    float p = t / 0.33;
    r = 0;  g = 255 * p;  b = 255 * (1 - p);
  } else if (t < 0.66) {
    float p = (t - 0.33) / 0.33;
    r = 255 * p;  g = 255;  b = 0;
  } else {
    float p = (t - 0.66) / 0.34;
    r = 255;  g = 255 * (1 - p) * 0.65;  b = 0;
  }

  // brightness scales with velocity: 15% floor up to 100%
  float bright = 0.15 + 0.85 * t;
  rgbLedWrite(LED_PIN, r * bright, g * bright, b * bright);
}

void setup() {
  MIDI.begin();
  USB.begin();
  Serial.begin(115200);
  analogReadResolution(12);
}

const unsigned long MASK_MS = 60; // ms before a normal retrigger
const unsigned long PEAK_WINDOW_MS = 5;   // how long to watch for the peak

// Hit Detection
int peak[16] = {0};
unsigned long peakStart[16] = {0};
unsigned long lastHit[16] = {0};
bool tracking[16] = {false};

// Flam Detection
int lastPeak[16] = {0};        // peak of the previous hit
int maskFloor[16] = {0};       // lowest value seen since last hit
int flamVotes[16] = {0};       // votes for if the flam is rising over multiple samples
const int FLAM_DELAY_MS = 10;        // MS before a flam can occur

// Note Trackers
unsigned long noteOffAt[16] = {0};
bool noteActive[16] = {false};

// Midi Out Map
const int NOTES[16] = {38, 36, 42, 46, 41, 43, 45, 47,
                       48, 50, 49, 51, 37, 39, 54, 56};

const int NUM_PADS = 1; // Used in the for loop for checking the signal from the multiplexer channels

void loop() {
  for (int ch = 0; ch < NUM_PADS; ch++) {
    if (noteActive[ch] && millis() >= noteOffAt[ch]) {
      MIDI.noteOff(NOTES[ch], 0, 1);
      noteActive[ch] = false;
      rgbLedWrite(LED_PIN, 0, 0, 0);   // off
    }
}

  for (int ch = 0; ch < NUM_PADS; ch++) {
    selectChannel(ch);
    delayMicroseconds(5); // Let multiplexer settle
    int v = analogRead(SIG_PIN);

    if (!tracking[ch]) {
      if (v > THRESHOLD && (millis() - lastHit[ch]) > MASK_MS) { // Hit Detected and Not Masking
        tracking[ch] = true;
        peak[ch] = v;
        peakStart[ch] = millis();
      } else {
        if (v < maskFloor[ch]) maskFloor[ch] = v;   // track the decay floor

        // Flam Detection
        bool risingAgain = (v > maskFloor[ch] + lastPeak[ch] * 2 / 3)
                && (v > THRESHOLD * 2) && (millis() - lastHit[ch] > FLAM_DELAY_MS);
        if (risingAgain) {
          // flam maybe? watch for a couple samples
          flamVotes[ch]++;
          if (flamVotes[ch] >= 15) {  // consecutive confirmations
            tracking[ch] = true;
            peak[ch] = v;
            peakStart[ch] = millis();
            flamVotes[ch] = 0;
          }
        } else {
          flamVotes[ch] = 0; // any dip resets the vote assuming no flam
        }
      }

    } else {
      // hit a note
      if (v > peak[ch]) peak[ch] = v;

      // process for it's peak
      if (millis() - peakStart[ch] >= PEAK_WINDOW_MS) {
        int velocity = constrain(map(peak[ch], THRESHOLD, MAX_SENSE, 5, 127), 1, 127);
        hitColor(velocity);

        if (noteActive[ch]) MIDI.noteOff(NOTES[ch], 0, 1); // Make sure the note turns off before the next hit
        MIDI.noteOn(NOTES[ch], velocity, 1);

        noteOffAt[ch] = millis() + NOTE_DURATION_MS; // set end time for the note
        noteActive[ch] = true;

        lastHit[ch] = millis();
        tracking[ch] = false;

        // start flam detection at max hit velocity
        lastPeak[ch] = velocity;
        maskFloor[ch] = velocity;
      }
    }
  }
}