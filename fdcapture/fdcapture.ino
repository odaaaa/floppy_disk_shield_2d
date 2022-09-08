// 2D/2DD Floppy disk capture shield control software

#include <stdio.h>
#include <limits.h>

#include <SPI.h>
#include <avr/pgmspace.h>

#include "fdd.h"
#include "spi_sram.h"
#include "fdcaptureshield.h"

size_t g_spin_ms;         // FDD spin speed (ms)

// GPIO mapping
#define SPISRAM_HOLD  (A4)
#define FD_TRK00      (A3)
#define FD_READY      (A2)
#define CAP_ACTIVE    (A1)
#define FD_INDEX      (A0)
#define SPI_SCK       (13)
#define SPI_MISO      (12)
#define SPI_MOSI      (11)
#define SPI_SS        (10)
#define FD_STEP       (9)
#define FD_DIR        (8)
#define FD_MOTOR      (7)
#define FD_HEAD_LOAD  (6)
#define FD_SIDE1      (5)
#define CAP_RST       (4)
#define CAP_EN        (3)
#define FD_WP         (2)

// Objects
FDD fdd;
SPISRAM spisram;
FDCaptureShield FDCap;

const unsigned long TRACK_CAPACITY_BYTE   = ((1024L*1024L)/8L); // 1Mbit SRAM full capacity

void dumpTrack_bit(unsigned long bytes = 0UL) {
  spisram.beginRead();

  if (bytes == 0) bytes = TRACK_CAPACITY_BYTE;
  for (unsigned long i = 0; i < bytes; i++) {
    byte dt = spisram.transfer(0);
    print8BIN(dt);
    //Serial.print(F(" "));
  }
  Serial.println(F(" "));
  spisram.endAccess();
}


void dumpTrack_hex(unsigned long bytes = 0UL) {

  Serial.println(F("**TRACK_DUMP_START"));
  spisram.beginRead();

  if (bytes == 0) bytes = TRACK_CAPACITY_BYTE;
  for (unsigned long i = 0; i < bytes; i++) {
    byte dt = spisram.transfer(0);
    printHex(dt);
    if (i % 64 == 63) {
      Serial.println(F(""));
    }
  }
  spisram.endAccess();
  Serial.println(F("**TRACK_DUMP_END"));
}


void dumpTrack_encode(unsigned long bytes = 0UL) {

  spisram.beginRead();

  if (bytes == 0) bytes = TRACK_CAPACITY_BYTE;

  int prev = 0;
  int count = 1;
  int b;
  int chr_count = 0;
  byte out;
  for (unsigned long i = 0; i < bytes; i++) {
    int dt = spisram.transfer(0);
    const byte encode_base = ' ';
    const byte max_length = 'z'-encode_base;
    const byte extend_char = '{';
    for (int p = 0; p < 8; p++) {
      b = (dt & 0x80u) ? 1 : 0;
      dt <<= 1;
      if (b == prev) {    // no pulse
        if(++count >= max_length) {
          if (chr_count == 0) {
            Serial.write('~');    // Start line code
          }
          Serial.write(extend_char);        // state extend character (extend pulse-to-pulse period without pulse)
          count -= max_length;
          chr_count++;
          if (chr_count == 99) {
            chr_count = 0;
            Serial.println("");
          }
        }
      } else {            // pulse
        out = count + encode_base;
        if (chr_count == 0) {
          Serial.write('~');    // Start line code
        }
        Serial.write(out);
        count = 1;
        chr_count++;
        if (chr_count == 99) {
          chr_count = 0;
          Serial.println("");
        }
      }
      prev = b;
    }
  }
  spisram.endAccess();
  Serial.println(F(""));
}


void histogram(unsigned long bytes = 0UL) {
  unsigned int histo[10];
  for (int i = 0; i < 10; i++) histo[i] = 0UL;

  spisram.beginRead();

  if (bytes == 0) bytes = TRACK_CAPACITY_BYTE;

  int prev = 0;
  int count = 1;
  for (unsigned long i = 0; i < bytes; i++) {
    byte dt = spisram.transfer(0);
    for (int j = 0; j < 8; j++) {
      int b = (dt & 0x80) == 0x80 ? 1 : 0;
      dt <<= 1;
      if (b == prev) {
        count++;
      } else {
        prev = b;
        histo[count]++;
        count = 1;
      }
    }
  }
  spisram.endAccess();

  for (int i = 0; i < 10; i++) {
    Serial.print(i);
    Serial.print(F(" : "));
    Serial.println(histo[i]);
  }
}


// Calibrate motor speed
void revolution_calibration(void) {
  float spin;
  while (1) {
    spin = fdd.measure_rpm();
    Serial.println(spin * 1000, DEC); // ms
  }
}


// Read single track
// read_overlap: overlap percentage to a spin. '5' means +5% (in total 105%) of 1 spin time.
void trackRead(int read_overlap) {
  spisram.beginWrite();
  spisram.hold(LOW);
  spisram.disconnect();           // Disconnect SPI SRAM from Arduino

  fdd.waitIndex();

  // Start captuering
  spisram.hold(HIGH);
  FDCap.enable();
  delay(g_spin_ms / 10);          // wait for 10% of spin

  fdd.waitIndex();
  delay((g_spin_ms * read_overlap) / 100);  // (overlap)% over capturing (read overlap)

  // Stop capturing
  digitalWrite(SPI_SS, HIGH);

  FDCap.disable();
  spisram.connect();
  spisram.endAccess();
}


// Read tracks  (track # = 0-79 (,83))
void read_tracks(int start_track, int end_track, int read_overlap) {
  fdd.track00();

  Serial.print("**SAMPLING_RATE 4000000\n");    // 4MHz is the default sampling rate of the Arduino FD shidld.

  const unsigned long capture_capacity_byte = 
    (unsigned long)((float)spisram.SPI_CLK * (float)g_spin_ms * ((float)(read_overlap+100)/100.f) ) / 8 / 1000;
  Serial.print(";CAPACITY[bytes]:");
  Serial.print(capture_capacity_byte);
  Serial.print("\n");

  size_t curr_trk = 0;
  for (size_t trk = start_track; trk <= end_track; trk++) {
    size_t fdd_track = trk / 2;
    size_t fdd_side  = trk % 2;
    fdd.seek(curr_trk, fdd_track);
    curr_trk = fdd_track;

    fdd.side(fdd_side);
    spisram.clear();

    Serial.print(F("**TRACK_READ "));
    Serial.print(fdd_track);
    Serial.print(F(" "));
    Serial.println(fdd_side);

    trackRead(read_overlap);

    dumpTrack_encode(capture_capacity_byte);
    //dumpTrack_encode(TRACK_CAPACITY_BYTE);    // SPI SRAM full dump
    Serial.println(F("**TRACK_END"));
  }
  //dumpTrack_hex(TRACK_CAPACITY_BYTE);
  //dumpTrack_bit(10);
}


// Memory test
void memory_test(void) {
  static int c = 0;
  fdd.waitIndex();
  spisram.fill(c++);
  spisram.dump(40);
  //dumpTrack_bit();
}


// Use access indicator LED as a timing light
// for FDD revolution adjustment
void timing_light(int freq) {
  int period = 1000 / freq;
  while (1) {
    digitalWrite(CAP_ACTIVE, HIGH);
    delay(period * 0.2);
    digitalWrite(CAP_ACTIVE, LOW);
    delay(period * 0.8);
  }
}


#define cmdBufSize (20)

void readLine(byte buf[], const size_t buf_size) {
  byte ch;
  size_t pos = 0;
  buf[0] = '\0';
  while(pos<buf_size) {
    while(Serial.available()==0);
    ch = Serial.read();
    if(ch == '\n') break;
    buf[pos++] = ch;
    buf[pos] = '\0';
  }
}


void setup() {
  // Make sure that the FD_capture board doesn't drive MOSI and SCK lines
  FDCap.init();
  spisram.init();

  Serial.begin(115200);

  fdd.init();                                     // Motor on, Head load on
  delay(200);

  spisram.clear();
}

// Command format
// "+R strack etrack mode overlap"
// "+T freq"
// "+V"
void loop() {
  byte cmdBuf[cmdBufSize+1];
  char cmd;

  Serial.println("");
  Serial.println(F("**FLOPPY DISK SHIELD FOR ARDUINO"));

  Serial.println(F("++CMD"));
  readLine(cmdBuf, cmdBufSize);
  cmd = toupper(cmdBuf[1]);

  if(cmd == 'R') {
    fdd.head(true);
    fdd.motor(true);
    enum FDD::ENUM_DRV_MODE media_mode = FDD::ENUM_DRV_MODE::mode_2d;
    int start_track, end_track;
    int read_overlap;   // track read overlap (%)
    sscanf(cmdBuf, "+%c %d %d %d %d", &cmd, &start_track, &end_track, &media_mode, &read_overlap);
    fdd.set_media_type(media_mode);
    Serial.println(F("**START"));

    // Detect FDD type (2D/2DD)
    fdd.detect_drive_type();
    fdd.track00();

    // Measure FDD spindle speed (measure time for 5 spins)
    float spin = 0.f;
    for(int i=0; i<5; i++) {
      spin += fdd.measure_rpm();
    }
    spin /= 5;
    Serial.print(F("**SPIN_SPD "));
    Serial.println(spin,8);
    g_spin_ms = (int)(spin*1000);

    int max_capture_time_ms = (int)(((spisram.SPISRAM_CAPACITY_BYTE*8.f) / spisram.SPI_CLK)*1000.f);
    int capture_time_ms     = (int)(g_spin_ms * (1.f + read_overlap/100.f));
    if(capture_time_ms > max_capture_time_ms) {
      read_overlap = (int)((((float)max_capture_time_ms / (float)g_spin_ms) - 1.f) * 100.f);
      Serial.print(F("##READ_OVERLAP IS LIMITED TO "));
      Serial.print(read_overlap);
      Serial.println(F("% BECAUSE THE AMOUNT OF CAPTURE DATA EXCEEDS THE MAX SPI-SRAM CAPACITY."));
    }
    Serial.print(F("**OVERLAP "));
    Serial.println(read_overlap);

    // Read all tracks
    read_tracks(start_track, end_track, read_overlap);

    Serial.println(F("**COMPLETED"));
  }
  if(cmd == 'T') {
    int freq;
    sscanf(cmdBuf, "+%c %d", &cmd, &freq);
    Serial.println(F("**Timing light mode"));
    timing_light(freq);
  }
  if(cmd == 'V') {
    Serial.println(F("**Revolution calibration mode"));
    revolution_calibration();
  }
  if(cmd == 'S') {
    Serial.println(F("**Seek mode"));
    int current_track = 0, target_track, side, val;
    fdd.track00();
    while(true) {
      readLine(cmdBuf, cmdBufSize);
      sscanf(cmdBuf, "%d", &val);
      if(val==0) {
        Serial.println(F("TRK00"));
        fdd.track00();
        target_track = 0;
        side = 0;
      } else {
        side         = val % 2;
        target_track = val / 2;
        fdd.seek(current_track, target_track);
      }
      fdd.side(side);
      Serial.print(target_track);
      Serial.print(" ");
      Serial.println(side);
      current_track = target_track;
    }
  }

  fdd.head(false);
  fdd.motor(false);

  // Halt
  //while (true) delay(100);
}
