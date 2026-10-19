// Host build of the murmur generator: renders a preview WAV and prints level / band stats.
//   g++ -O2 -std=c++17 -Isrc tools/murmur_preview.cpp src/murmur.cpp -o /tmp/murmur_preview
//   /tmp/murmur_preview docs/murmur_preview.wav 40 5     (file, seconds, voices)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "murmur.h"

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : "murmur_preview.wav";
  int secs = argc > 2 ? atoi(argv[2]) : 40;
  int voices = argc > 3 ? atoi(argv[3]) : 5;
  Murmur m; m.begin(12345); m.setVoices(voices);
  int n = secs * 8000;
  std::vector<int8_t> pcm(n);
  double sq = 0; int peak = 0;
  for (int i = 0; i < n; i++) { pcm[i] = m.next(); sq += pcm[i] * (double)pcm[i]; if (abs(pcm[i]) > peak) peak = abs(pcm[i]); }
  printf("voices=%d  rms=%.1f  peak=%d (of 127)\n", voices, sqrt(sq / n), peak);
  // crude band energy via a few DFT bins over 1 s windows
  double band[4] = {0, 0, 0, 0}; const double edges[5] = {50, 500, 1500, 3000, 4000};
  for (int w = 0; w + 1024 <= n; w += 8000) {
    for (int b = 0; b < 4; b++) for (double f = edges[b]; f < edges[b + 1]; f += 50) {
      double re = 0, im = 0;
      for (int i = 0; i < 1024; i++) { double a = 2 * M_PI * f * i / 8000; re += pcm[w + i] * cos(a); im += pcm[w + i] * sin(a); }
      band[b] += re * re + im * im;
    }
  }
  double tot = band[0] + band[1] + band[2] + band[3];
  printf("energy share: 50-500Hz %.0f%%  500-1500 %.0f%%  1500-3000 %.0f%%  3000-4000 %.0f%%\n",
         100 * band[0] / tot, 100 * band[1] / tot, 100 * band[2] / tot, 100 * band[3] / tot);
  FILE* f = fopen(out, "wb");
  auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); }; auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); w32(36 + n); fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(1); w16(1); w32(8000); w32(8000); w16(1); w16(8);
  fwrite("data", 1, 4, f); w32(n);
  for (int i = 0; i < n; i++) { unsigned char b = (unsigned char)(pcm[i] + 128); fwrite(&b, 1, 1, f); }
  fclose(f);
  return 0;
}
