#include "ai/edit_journal.hpp"

namespace tuide {

EditJournalStore& EditJournalStore::instance() {
  static EditJournalStore store;
  return store;
}

uint64_t EditJournalStore::next_op_id() {
  return 1;
}

void EditJournalStore::record_edit(const std::string&, AiTextEdit) {}

void EditJournalStore::mark_lines(const std::string&, int, int, AiAuthor, uint64_t) {}

AiAuthor EditJournalStore::author_for_line(const std::string&, int) const {
  return AiAuthor::Human;
}

bool EditJournalStore::line_is_ai(const std::string&, int) const {
  return false;
}

bool EditJournalStore::apply_replace(WorkspaceModel*, const std::string&, int, int, int, int,
                                     const std::string&, AiAuthor, std::string* error) {
  if (error) {
    *error = "edit_journal stub";
  }
  return false;
}

bool EditJournalStore::apply_demo(WorkspaceModel*, std::string* detail) {
  if (detail) {
    *detail = "stub";
  }
  return false;
}

void EditJournalStore::save_sidecar(const std::string&, const std::string&) const {}

void EditJournalStore::load_sidecar(const std::string&, const std::string&, const std::string&) {}

void EditJournalStore::invalidate(const std::string&) {}

std::string EditJournalStore::hash_text(const std::string&) {
  return {};
}

std::string EditJournalStore::sidecar_path(const std::string&, const std::string&) {
  return {};
}

EditJournalStore::FileJournal* EditJournalStore::mutable_journal(const std::string&) {
  return nullptr;
}

const EditJournalStore::FileJournal* EditJournalStore::journal(const std::string&) const {
  return nullptr;
}

}  // namespace tuide
