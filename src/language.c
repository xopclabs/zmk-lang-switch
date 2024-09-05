#include <zmk/language.h>

#include <stdint.h>

uint8_t zmk_language_state() { return current_language_state; }