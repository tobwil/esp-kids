// ARD Audiothek: Sendungen und Folgen über die öffentliche GraphQL-Schnittstelle (ohne Schlüssel).
// Folgen sind MP3-Dateien und laufen über den MP3-Spieler (player.h), mit gemerkter Position.
#pragma once
#include <Arduino.h>
#include "music.h"

namespace ard {

// Listen: "ard:home", "ard:cat:<Rubrik>", "ard:set:<Sendung>", "ardsearch:<Begriff>",
//         "ard:live" (alle ARD-Radiosender)
// Folgen sind Titel mit der Kennung "ard:a:<MP3-Adresse>"; Livestreams sind Sender ("radio:<Adresse>", webradio.h)
bool browse(const char* path, char* title, size_t titleLen, music::Item* items, int max, int* count, char* err,
            size_t errLen);
// Sendungsname, Bild und Sendungs-Kennung („ard:set:…“) zu einer Folge (aus den zuletzt geladenen Listen)
void meta(const char* audioUrl, String& show, String& image, String& setPath);

}  // namespace ard
