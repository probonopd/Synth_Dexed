#ifndef ARDUINO

#if defined(_WIN32)
#include "main_win.h"
#elif defined(__APPLE__)
#include "main_mac.h"
#else
#include "main_linux.h"
#endif
#include "main_common.h"


int main(int argc, char* argv[]) {
#if defined(_WIN32)
    return main_common_entry(argc, argv, get_win_hooks());
#elif defined(__APPLE__)
    return main_common_entry(argc, argv, get_mac_hooks());
#else
    return main_common_entry(argc, argv, get_linux_hooks());
#endif
}

#endif