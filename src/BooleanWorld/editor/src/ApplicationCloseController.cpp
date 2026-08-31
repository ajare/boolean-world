#include "ApplicationCloseController.h"

namespace editor {

bool ApplicationCloseController::confirmationPending() const {
  return mConfirmationPending;
}

ApplicationCloseResult ApplicationCloseController::requestClose(
    bool hasWorld, bool modified) {
  if (hasWorld && modified) {
    mConfirmationPending = true;
    return ApplicationCloseResult::KeepRunning;
  }
  mConfirmationPending = false;
  return ApplicationCloseResult::Close;
}

ApplicationCloseResult ApplicationCloseController::saveCompleted(bool succeeded) {
  if (!mConfirmationPending || !succeeded) {
    return ApplicationCloseResult::KeepRunning;
  }
  mConfirmationPending = false;
  return ApplicationCloseResult::Close;
}

ApplicationCloseResult ApplicationCloseController::discard() {
  if (!mConfirmationPending) {
    return ApplicationCloseResult::KeepRunning;
  }
  mConfirmationPending = false;
  return ApplicationCloseResult::Close;
}

void ApplicationCloseController::cancel() {
  mConfirmationPending = false;
}

}  // namespace editor
