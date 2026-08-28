#ifndef TRANSLATABLE_LABEL_H
#define TRANSLATABLE_LABEL_H

// Marks driver-supplied display text as English prose rather than a signal name.
// Expands to nothing, so deleting it frees no bytes and silently drops the string
// from every language pack.
#define TL(text) text

#endif
