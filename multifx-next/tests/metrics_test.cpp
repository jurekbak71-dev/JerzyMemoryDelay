#include "audio_audit.hpp"
#include "rt_metrics.hpp"
#include <stdexcept>
int main() {
  AudioAudit audit;
  nextfx::TimeHistogram histogram;
  audited_audio([&] {
    histogram.record(0);
    histogram.record(1500);
    histogram.record(4095000);
    histogram.record(9999999);
  });
  auto h = histogram.snapshot();
  if (h[0] != 1 || h[1] != 1 || h[4095] != 2)
    throw std::runtime_error("histogram boundaries/censoring");
  audit.clean();
}
