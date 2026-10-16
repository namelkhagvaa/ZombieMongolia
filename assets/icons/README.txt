Zombie Mongolia UI icon pack
================================

Files:
- attack_icon_1.png
- attack_icon_2.png
- upgrade_1_strength.png
- upgrade_2_defense.png
- upgrade_3_health.png
- upgrade_4_inventory.png
- upgrade_5_speed.png

Each image is a 512x512 PNG with an alpha channel, suitable for UI prototypes.
These were cropped from a generated concept sheet and the dark background was
removed approximately; inspect edges in-game and adjust alpha if needed.

Suggested C++/raylib usage:
Texture2D icon = LoadTexture("assets/ui/icons/upgrade_1_strength.png");
DrawTexturePro(icon, sourceRect, destRect, origin, 0.0f, WHITE);
UnloadTexture(icon); // only when the texture is no longer needed
