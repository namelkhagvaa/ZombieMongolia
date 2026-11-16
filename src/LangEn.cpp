// Англи орчуулга: { кодон дахь монгол текст, англи } (src/Lang.h: Tr). Түлхүүр нь кодынхтой яг ижил бичлэгтэй
// (\n, \xE2... хэвээр), TextFormat-ийн %d, %s ижил дараалалтай. tools/lang_strings.py орчуулгагүйг жагсаана.
extern const char* const kEnglishPairs[][2] = {
#include "lang/en_ui.inc"
#include "lang/en_account.inc"
#include "lang/en_campaign.inc"
#include "lang/en_missions.inc"
#include "lang/en_game.inc"
#include "lang/en_game2.inc"
#include "lang/en_game3.inc"
};
extern const int kEnglishCount = (int)(sizeof(kEnglishPairs) / sizeof(kEnglishPairs[0]));
