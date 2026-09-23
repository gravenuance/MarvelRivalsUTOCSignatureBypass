#pragma once

// Runs both locators against a game executable on disk and prints where they land and how long they take.
int RunProbe(const wchar_t* executable);

// Runs the unmount policy over every pak under <paks>/~mods, read-only, and prints the verdicts and the time taken.
int RunClassify(const wchar_t* paksDirectory);
