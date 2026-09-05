#include <iostream>
#include <stdexcept>
#include <string>

#include "ApplicationCloseController.h"

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

void cleanOrAbsentWorldClosesImmediately() {
  editor::ApplicationCloseController absent;
  require(absent.requestClose(false, false) == editor::ApplicationCloseResult::Close &&
              !absent.confirmationPending(),
          "closing without a World did not proceed immediately");

  editor::ApplicationCloseController clean;
  require(clean.requestClose(true, false) == editor::ApplicationCloseResult::Close &&
              !clean.confirmationPending(),
          "closing a clean World did not proceed immediately");
}

void modifiedWorldRequiresAnExplicitDecision() {
  editor::ApplicationCloseController controller;
  require(controller.requestClose(true, true) ==
                  editor::ApplicationCloseResult::KeepRunning &&
              controller.confirmationPending(),
          "closing a modified World did not request confirmation");

  controller.cancel();
  require(!controller.confirmationPending(),
          "Cancel did not dismiss close confirmation");
  require(controller.requestClose(true, true) ==
                  editor::ApplicationCloseResult::KeepRunning &&
              controller.confirmationPending(),
          "the editor could not request close again after Cancel");
}

void saveOnlyClosesAfterSuccessAndDiscardClosesImmediately() {
  editor::ApplicationCloseController saving;
  require(saving.requestClose(true, true) ==
              editor::ApplicationCloseResult::KeepRunning,
          "the saving fixture did not request confirmation");
  require(saving.saveCompleted(false) ==
                  editor::ApplicationCloseResult::KeepRunning &&
              saving.confirmationPending(),
          "a cancelled or failed save closed the editor");
  require(saving.saveCompleted(true) == editor::ApplicationCloseResult::Close &&
              !saving.confirmationPending(),
          "a successful save did not close the editor");

  editor::ApplicationCloseController discarding;
  require(discarding.requestClose(true, true) ==
              editor::ApplicationCloseResult::KeepRunning,
          "the discard fixture did not request confirmation");
  require(discarding.discard() == editor::ApplicationCloseResult::Close &&
              !discarding.confirmationPending(),
          "Don't Save did not close the editor");
}

}  // namespace

int main() {
  try {
    cleanOrAbsentWorldClosesImmediately();
    modifiedWorldRequiresAnExplicitDecision();
    saveOnlyClosesAfterSuccessAndDiscardClosesImmediately();
    std::cout << "Application close decisions passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
