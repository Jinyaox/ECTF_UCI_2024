#include "xor.h"

void XOR_secure(unsigned char* arr1, unsigned char* arr2, int size, unsigned char* dest){
    for (int i = 0; i < size; i++) {
        dest[i] = arr1[i] ^ arr2[i];
    }
    return;
}