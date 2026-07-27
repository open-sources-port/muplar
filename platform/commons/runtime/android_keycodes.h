#pragma once

#include <cstdint>

namespace muplar::runtime
{

inline int32_t android_key_code_from_mac_key(uint16_t key_code)
{
    switch (key_code) {
    case 0:
        return 29;  // A
    case 1:
        return 47;  // S
    case 2:
        return 32;  // D
    case 3:
        return 34;  // F
    case 4:
        return 36;  // H
    case 5:
        return 35;  // G
    case 6:
        return 54;  // Z
    case 7:
        return 52;  // X
    case 8:
        return 31;  // C
    case 9:
        return 50;  // V
    case 11:
        return 30;  // B
    case 12:
        return 45;  // Q
    case 13:
        return 51;  // W
    case 14:
        return 33;  // E
    case 15:
        return 46;  // R
    case 16:
        return 53;  // Y
    case 17:
        return 48;  // T
    case 18:
        return 8;  // 1
    case 19:
        return 9;  // 2
    case 20:
        return 10;  // 3
    case 21:
        return 11;  // 4
    case 22:
        return 13;  // 6
    case 23:
        return 12;  // 5
    case 24:
        return 70;  // =
    case 25:
        return 16;  // 9
    case 26:
        return 14;  // 7
    case 27:
        return 69;  // -
    case 28:
        return 15;  // 8
    case 29:
        return 7;  // 0
    case 30:
        return 72;  // ]
    case 31:
        return 43;  // O
    case 32:
        return 49;  // U
    case 33:
        return 71;  // [
    case 34:
        return 37;  // I
    case 35:
        return 44;  // P
    case 36:
        return 66;  // Enter
    case 37:
        return 40;  // L
    case 38:
        return 38;  // J
    case 39:
        return 75;  // '
    case 40:
        return 39;  // K
    case 41:
        return 74;  // ;
    case 42:
        return 73;  // Backslash
    case 43:
        return 55;  // ,
    case 44:
        return 76;  // /
    case 45:
        return 42;  // N
    case 46:
        return 41;  // M
    case 47:
        return 56;  // .
    case 48:
        return 61;  // Tab
    case 49:
        return 62;  // Space
    case 50:
        return 68;  // `
    case 51:
        return 67;  // Delete
    case 53:
        return 111;  // Escape
    case 123:
        return 21;  // Left
    case 124:
        return 22;  // Right
    case 125:
        return 20;  // Down
    case 126:
        return 19;  // Up
    default:
        return 0;
    }
}

}  // namespace muplar::runtime
