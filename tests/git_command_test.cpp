#include "git/git_command.hpp"

#include <iostream>
#include <string>

namespace {

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "git_command_test failed: " << msg << '\n';
    std::abort();
  }
}

}  // namespace

int main() {
  using tuide::git_output_requires_credentials;

  expect(git_output_requires_credentials(
             "fatal: could not read Username for 'https://github.com': "
             "terminal prompts disabled\n"),
         "https username prompt disabled");
  expect(git_output_requires_credentials("remote: Invalid username or password.\n"
                                         "fatal: Authentication failed for "
                                         "'https://example.com/repo.git'\n"),
         "https auth failed");
  expect(git_output_requires_credentials(
             "Permission denied (publickey).\n"
             "fatal: Could not read from remote repository.\n"),
         "ssh publickey");
  expect(git_output_requires_credentials("Enter passphrase for key '/home/u/.ssh/id_rsa':\n"),
         "ssh passphrase prompt");
  expect(git_output_requires_credentials("error: failed to execute prompt script (exit 1)\n"),
         "askpass failed");
  expect(!git_output_requires_credentials("Already up to date.\n"), "success is not auth");
  expect(!git_output_requires_credentials(
             "error: Your local changes to the following files would be overwritten by merge\n"),
         "merge conflict is not auth");

  std::cout << "git_command_test OK\n";
  return 0;
}
