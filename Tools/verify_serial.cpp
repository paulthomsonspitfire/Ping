// Quick test: verify a name+serial against LicenceVerifier
// Compile: g++ -std=c++17 -I../Source -o verify_serial verify_serial.cpp -lsodium

#include "../Source/LicenceVerifier.h"
#include <iostream>

int main()
{
    if (sodium_init() < 0) {
        std::cerr << "sodium_init failed\n";
        return 1;
    }
    LicenceVerifier v;
    auto r = v.activate("Andy Blaney", "ACSIH-GWSRS-32NTP-BEZKS-ZWDZ6-BY4EG-A3LWM-XLMQN-EFWJM-Y23WU-E32DQ-RWO7H-O4KRI-DARRA-DBM3G-G5UUN-VGHMX-VVH6Q-ZNFL3-KWHGK-SDDBN-ZSHSI-DCNRQ-W4ZLZ-PRYHE-334GI-YDENZ-NGEZC-2MZR");
    std::cout << "valid: " << (r.valid ? "yes" : "no") << "\n";
    if (!r.valid)
        std::cout << "error: " << r.errorMessage << "\n";
    else
        std::cout << "tier: " << r.tier << ", expiry: " << r.expiry << ", name: " << r.normalisedName << "\n";
    return r.valid ? 0 : 1;
}
