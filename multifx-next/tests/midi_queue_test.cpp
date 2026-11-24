#include "midi_queue.hpp"
#include <stdexcept>
#include <thread>
int main() {
  nextfx::MidiQueue q;
  JfxMidi m{};
  for (unsigned i = 0; i < 1024; i++) {
    m.frame = i;
    if (!q.push(m))
      throw std::runtime_error("early full");
  }
  if (q.push(m))
    throw std::runtime_error("overflow accepted");
  for (unsigned i = 0; i < 1024; i++) {
    if (!q.pop(m) || m.frame != i)
      throw std::runtime_error("order");
  }
  if (q.pop(m))
    throw std::runtime_error("underflow");
  std::thread producer([&] {
    for (unsigned i = 0; i < 100000; i++) {
      JfxMidi n{};
      n.frame = i;
      while (!q.push(n))
        std::this_thread::yield();
    }
  });
  for (unsigned i = 0; i < 100000; i++) {
    while (!q.pop(m))
      std::this_thread::yield();
    if (m.frame != i)
      throw std::runtime_error("concurrent order");
  }
  producer.join();
}
