#include "ai/job.hpp"

int pending_insert;

void handle_key() {
  pending_insert = 1;
  cancel_job();
}
