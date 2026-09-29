#include "../board/Board.h"
#include <esp_app_desc.h>
namespace {
constexpr esp_app_desc_t descriptor() {
  esp_app_desc_t d{};
  d.magic_word = ESP_APP_DESC_MAGIC_WORD;
  constexpr char project[] = "MILESTONE-NOVA";
  for (unsigned i = 0; i < sizeof(project); ++i)
    d.project_name[i] = project[i];
  for (unsigned i = 0; i < sizeof(nova::board::version); ++i)
    d.version[i] = nova::board::version[i];
  d.max_efuse_blk_rev_full = 0xffff;
  d.mmu_page_size = 16;
  return d;
}
} // namespace
// Replace the SDK's weak arduino-lib-builder descriptor. The signed image contains
// its real product/version identity at the standard ESP image descriptor offset.
extern "C" const esp_app_desc_t esp_app_desc __attribute__((section(".rodata_desc"), used)) =
    descriptor();
