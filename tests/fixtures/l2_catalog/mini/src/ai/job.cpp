#include "ai/job.hpp"

int busy;

void cancel_job() {
  busy = 0;
}
