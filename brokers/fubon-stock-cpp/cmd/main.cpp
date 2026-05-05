#include <chrono>
#include <iostream>
#include <thread>

#include "../src/latency_measurement.h"

int main() {
  try {
    std::locale::global(std::locale("en_US.UTF-8"));

    LatencyMeasurement latency_test;

    if (!latency_test.initialize()) {
      std::cerr << "Failed to initialize connection" << std::endl;
      return 1;
    }

    // Wait for connection to stabilize before sending orders
    std::this_thread::sleep_for(std::chrono::seconds(1));
    latency_test.runLatencyTest();

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
