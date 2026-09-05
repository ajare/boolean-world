#pragma once

namespace editor {

enum class ApplicationCloseResult {
  KeepRunning,
  Close
};

class ApplicationCloseController {
private:
  bool mConfirmationPending{false};

public:
  [[nodiscard]] bool confirmationPending() const;
  [[nodiscard]] ApplicationCloseResult requestClose(bool hasWorld, bool modified);
  [[nodiscard]] ApplicationCloseResult saveCompleted(bool succeeded);
  [[nodiscard]] ApplicationCloseResult discard();
  void cancel();
};

}  // namespace editor
