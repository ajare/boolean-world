#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Document.h"

namespace editor {
typedef std::function<void(Document*)> DocumentHelperFunction;

void newDocument(editor::Document* doc);

void openDocument(editor::Document* doc);

[[nodiscard]] std::vector<std::string> const& recentWorldPaths();

void openRecentDocument(editor::Document* doc, std::string const& filepath);

void renderRecentWorldMissingDialog();

void saveDocumentAs(editor::Document* doc);

void saveDocument(editor::Document* doc);

// Requests application close. A modified active World defers approval until
// renderApplicationCloseDialog receives an explicit user decision.
void exitApp(editor::Document* doc);

void renderApplicationCloseDialog(editor::Document* doc);

[[nodiscard]] bool applicationCloseApproved();

void showHelp(editor::Document* doc);

void checkModifiedOperation(editor::Document* doc, std::string const& title, DocumentHelperFunction func);

void handleModifiedDocument(editor::Document* doc, bool docAction, bool checkDocumentModified, std::string const& docText, DocumentHelperFunction helperFunc);

void handleNonDocumentAction(std::string const& action);

void checkNonDocumentOperation();

void renderHelp();

bw::core::World* loadWorld(std::string const& filepath);

// Pans and zooms so every selected Primitive fits the World window with a
// small margin, à la Blender's numpad "." (View Selected). With nothing
// selected, resets both pan and zoom to their defaults instead.
void goHome(editor::Document* doc);

// Blender's View > Frame All (Home key): pans to the world's centre and
// zooms so its full extents fit the World window, regardless of selection -
// unlike goHome(), which frames the selection (or, with none, resets to the
// default view).
void frameAllWorld(editor::Document* doc);

void enableGhost(editor::Document* doc, bool enable);

void selectAndHomeGhost(editor::Document* doc);

}  // namespace editor
