#include "../../Software/src/devboard/webserver/web_ui_selection.h"

namespace {

const uint8_t kFixtureData[] = {0};

const WebAsset kFixtureAssets[] = {
    {"/app.css", kFixtureData, 1, "text/css", "\"a\""},
    {"/shell-legacy.html", kFixtureData, 1, "text/html", "\"b\""},
    {"/shell-modern.html", kFixtureData, 1, "text/html", "\"c\""},
};

}  // namespace

UiShellTable default_ui_shell_table() {
  return UiShellTable{kFixtureAssets, 3};
}
