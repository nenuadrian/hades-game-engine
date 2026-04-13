#ifndef HADES_ENGINE_RUNTIME_HADES_KEYCODES_HPP
#define HADES_ENGINE_RUNTIME_HADES_KEYCODES_HPP

namespace hades
{
  // Keyboard keys (matching SDL2 keycodes)
  constexpr int HADES_KEY_RETURN = 13;
  constexpr int HADES_KEY_ESCAPE = 27;
  constexpr int HADES_KEY_SPACE = 32;
  constexpr int HADES_KEY_TAB = 9;
  constexpr int HADES_KEY_BACKSPACE = 8;
  constexpr int HADES_KEY_DELETE = 127;

  // Number keys
  constexpr int HADES_KEY_0 = 48;
  constexpr int HADES_KEY_1 = 49;
  constexpr int HADES_KEY_2 = 50;
  constexpr int HADES_KEY_3 = 51;
  constexpr int HADES_KEY_4 = 52;
  constexpr int HADES_KEY_5 = 53;
  constexpr int HADES_KEY_6 = 54;
  constexpr int HADES_KEY_7 = 55;
  constexpr int HADES_KEY_8 = 56;
  constexpr int HADES_KEY_9 = 57;

  // Letter keys (lowercase ASCII)
  constexpr int HADES_KEY_A = 97;
  constexpr int HADES_KEY_B = 98;
  constexpr int HADES_KEY_C = 99;
  constexpr int HADES_KEY_D = 100;
  constexpr int HADES_KEY_E = 101;
  constexpr int HADES_KEY_F = 102;
  constexpr int HADES_KEY_G = 103;
  constexpr int HADES_KEY_H = 104;
  constexpr int HADES_KEY_I = 105;
  constexpr int HADES_KEY_J = 106;
  constexpr int HADES_KEY_K = 107;
  constexpr int HADES_KEY_L = 108;
  constexpr int HADES_KEY_M = 109;
  constexpr int HADES_KEY_N = 110;
  constexpr int HADES_KEY_O = 111;
  constexpr int HADES_KEY_P = 112;
  constexpr int HADES_KEY_Q = 113;
  constexpr int HADES_KEY_R = 114;
  constexpr int HADES_KEY_S = 115;
  constexpr int HADES_KEY_T = 116;
  constexpr int HADES_KEY_U = 117;
  constexpr int HADES_KEY_V = 118;
  constexpr int HADES_KEY_W = 119;
  constexpr int HADES_KEY_X = 120;
  constexpr int HADES_KEY_Y = 121;
  constexpr int HADES_KEY_Z = 122;

  // Arrow keys (SDL2 scancodes with SDLK_SCANCODE_MASK)
  constexpr int HADES_KEY_RIGHT = 1073741903;
  constexpr int HADES_KEY_LEFT = 1073741904;
  constexpr int HADES_KEY_DOWN = 1073741905;
  constexpr int HADES_KEY_UP = 1073741906;

  // Modifier keys
  constexpr int HADES_KEY_LSHIFT = 1073742049;
  constexpr int HADES_KEY_RSHIFT = 1073742053;
  constexpr int HADES_KEY_LCTRL = 1073742048;
  constexpr int HADES_KEY_RCTRL = 1073742052;
  constexpr int HADES_KEY_LALT = 1073742050;
  constexpr int HADES_KEY_RALT = 1073742054;

  // Function keys
  constexpr int HADES_KEY_F1 = 1073741882;
  constexpr int HADES_KEY_F2 = 1073741883;
  constexpr int HADES_KEY_F3 = 1073741884;
  constexpr int HADES_KEY_F4 = 1073741885;
  constexpr int HADES_KEY_F5 = 1073741886;
  constexpr int HADES_KEY_F6 = 1073741887;
  constexpr int HADES_KEY_F7 = 1073741888;
  constexpr int HADES_KEY_F8 = 1073741889;
  constexpr int HADES_KEY_F9 = 1073741890;
  constexpr int HADES_KEY_F10 = 1073741891;
  constexpr int HADES_KEY_F11 = 1073741892;
  constexpr int HADES_KEY_F12 = 1073741893;

  // Mouse buttons (matching SDL2 button constants)
  constexpr int HADES_MOUSE_LEFT = 1;
  constexpr int HADES_MOUSE_MIDDLE = 2;
  constexpr int HADES_MOUSE_RIGHT = 3;
}

#endif
