#include "build/build_environment_selector.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <filesystem>

namespace fs = std::filesystem;

namespace tgdb {

namespace {

constexpr int kScoreRecentArtifact = 40;
constexpr int kScorePreviousEnvironment = 25;
constexpr int kScoreActiveFileInMarker = 30;
constexpr int kScoreTerminalCwdMatch = 35;
constexpr int kSwitchHysteresis = 15;
constexpr int kRecentArtifactMinutes = 30;

bool path_is_under(const std::string& path, const std::string& root) {
  if (path.empty() || root.empty()) {
    return false;
  }
  std::error_code ec;
  const fs::path canonical_path = fs::weakly_canonical(fs::path(path), ec);
  const fs::path canonical_root = fs::weakly_canonical(fs::path(root), ec);
  const std::string normalized_path = ec ? path : canonical_path.string();
  const std::string normalized_root = ec ? root : canonical_root.string();
  if (normalized_path.size() < normalized_root.size()) {
    return false;
  }
  return normalized_path.compare(0, normalized_root.size(), normalized_root) == 0 &&
         (normalized_path.size() == normalized_root.size() ||
          normalized_path[normalized_root.size()] == '/');
}

bool marker_recently_touched(const std::string& marker_path) {
  std::error_code ec;
  if (!fs::exists(marker_path, ec)) {
    return false;
  }
  const auto mtime = fs::last_write_time(marker_path, ec);
  if (ec) {
    return false;
  }
  const auto now = fs::file_time_type::clock::now();
  return (now - mtime) < std::chrono::minutes(kRecentArtifactMinutes);
}

}  // namespace

int score_environment(const BuildEnvironment& env, const EnvironmentSelectionHints& hints,
                      const std::string& previous_environment_id) {
  int score = 0;
  for (const auto& marker : env.marker_paths) {
    if (marker_recently_touched(marker)) {
      score += kScoreRecentArtifact;
      break;
    }
  }

  if (!previous_environment_id.empty() && env.id == previous_environment_id) {
    score += kScorePreviousEnvironment;
  }

  if (!hints.active_file_path.empty()) {
    for (const auto& marker : env.marker_paths) {
      if (path_is_under(hints.active_file_path, marker) ||
          path_is_under(marker, hints.active_file_path)) {
        score += kScoreActiveFileInMarker;
        break;
      }
    }
  }

  if (!hints.terminal_cwd.empty()) {
    if (path_is_under(hints.terminal_cwd, env.working_dir) ||
        env.working_dir == hints.terminal_cwd) {
      score += kScoreTerminalCwdMatch;
    }
  }

  return score;
}

EnvironmentSelectionResult select_active_environment(
    const std::vector<BuildEnvironment>& candidates, const std::string& configured_active_id,
    const EnvironmentSelectionHints& hints, const std::string& previous_environment_id) {
  EnvironmentSelectionResult result;
  if (candidates.empty()) {
    return result;
  }

  if (!configured_active_id.empty() && configured_active_id != "auto") {
    const auto selected = std::find_if(
        candidates.begin(), candidates.end(), [&](const BuildEnvironment& env) {
          return env.id == configured_active_id;
        });
    if (selected != candidates.end()) {
      result.environment = *selected;
      result.environment.score = score_environment(result.environment, hints, previous_environment_id);
      result.changed = result.environment.id != previous_environment_id;
      return result;
    }
  }

  BuildEnvironment best = candidates.front();
  int best_score = -1;
  for (const auto& candidate : candidates) {
    const int candidate_score = score_environment(candidate, hints, previous_environment_id);
    if (candidate_score > best_score) {
      best_score = candidate_score;
      best = candidate;
    }
  }
  best.score = best_score;

  if (!previous_environment_id.empty() && best.id != previous_environment_id) {
    const auto previous = std::find_if(
        candidates.begin(), candidates.end(),
        [&](const BuildEnvironment& env) { return env.id == previous_environment_id; });
    if (previous != candidates.end()) {
      const int previous_score = score_environment(*previous, hints, previous_environment_id);
      if (best_score - previous_score < kSwitchHysteresis) {
        best = *previous;
        best.score = previous_score;
      }
    }
  }

  result.environment = best;
  result.changed = best.id != previous_environment_id;
  return result;
}

}  // namespace tgdb
