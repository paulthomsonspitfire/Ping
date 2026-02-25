// =============================================================================
// keygen.cpp  —  Serial number generator for P!NG
// =============================================================================
//
// DEPENDENCIES:
//   libsodium  (https://libsodium.org)
//   Install on Mac:  brew install libsodium
//
// COMPILE:
//   g++ -std=c++17 -o keygen keygen.cpp -lsodium
//
// USAGE:
//   First run (generates your key pair — do this ONCE and keep keys safe):
//     ./keygen --generate-keys
//
//   Generate a serial for a customer:
//     ./keygen --name "Paul Hartnoll" --tier pro --expiry 2027-12-31
//
//   Tiers:  demo | standard | pro
//
// IMPORTANT:
//   - Keep private_key.bin SECRET. Never ship it, never commit it to git.
//   - Paste the PUBLIC_KEY output into Source/LicenceVerifier.h
//   - Back up private_key.bin somewhere safe offline.
// =============================================================================

#include <sodium.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstring>

std::string normaliseName(const std::string& name)
{
    std::string result;
    bool lastWasSpace = true;

    for (char c : name)
    {
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            if (!lastWasSpace)
                result += ' ';
            lastWasSpace = true;
        }
        else
        {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            lastWasSpace = false;
        }
    }

    if (!result.empty() && result.back() == ' ')
        result.pop_back();

    return result;
}

static const char BASE32_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

std::string base32Encode(const std::vector<uint8_t>& data)
{
    std::string result;
    int buffer = 0;
    int bitsLeft = 0;

    for (uint8_t byte : data)
    {
        buffer <<= 8;
        buffer |= byte;
        bitsLeft += 8;

        while (bitsLeft >= 5)
        {
            bitsLeft -= 5;
            result += BASE32_ALPHABET[(buffer >> bitsLeft) & 0x1F];
        }
    }

    if (bitsLeft > 0)
    {
        buffer <<= (5 - bitsLeft);
        result += BASE32_ALPHABET[buffer & 0x1F];
    }

    return result;
}

std::string formatSerial(const std::string& raw)
{
    std::string formatted;
    for (size_t i = 0; i < raw.size(); ++i)
    {
        if (i > 0 && i % 5 == 0)
            formatted += '-';
        formatted += raw[i];
    }
    return formatted;
}

void generateKeys()
{
    uint8_t publicKey[crypto_sign_PUBLICKEYBYTES];
    uint8_t privateKey[crypto_sign_SECRETKEYBYTES];

    crypto_sign_keypair(publicKey, privateKey);

    {
        std::ofstream f("private_key.bin", std::ios::binary);
        f.write(reinterpret_cast<const char*>(privateKey), crypto_sign_SECRETKEYBYTES);
        std::cout << "Written: private_key.bin  *** KEEP THIS SECRET ***\n";
    }

    {
        std::ofstream f("public_key.bin", std::ios::binary);
        f.write(reinterpret_cast<const char*>(publicKey), crypto_sign_PUBLICKEYBYTES);
        std::cout << "Written: public_key.bin\n";
    }

    std::cout << "\nPaste this into Source/LicenceVerifier.h as PUBLIC_KEY:\n\n";
    std::cout << "static const uint8_t PUBLIC_KEY[" << crypto_sign_PUBLICKEYBYTES << "] = {\n    ";
    for (size_t i = 0; i < crypto_sign_PUBLICKEYBYTES; ++i)
    {
        std::cout << "0x" << std::hex << std::uppercase
                  << (static_cast<int>(publicKey[i]) < 16 ? "0" : "")
                  << static_cast<int>(publicKey[i]);
        if (i < crypto_sign_PUBLICKEYBYTES - 1) std::cout << ", ";
        if ((i + 1) % 8 == 0) std::cout << "\n    ";
    }
    std::cout << std::dec << "\n};\n";
}

std::string generateSerial(const std::string& customerName,
                            const std::string& tier,
                            const std::string& expiry)
{
    std::ifstream f("private_key.bin", std::ios::binary);
    if (!f)
    {
        std::cerr << "Error: private_key.bin not found. Run --generate-keys first.\n";
        return "";
    }

    uint8_t privateKey[crypto_sign_SECRETKEYBYTES];
    f.read(reinterpret_cast<char*>(privateKey), crypto_sign_SECRETKEYBYTES);

    std::string payload = normaliseName(customerName) + "|" + tier + "|" + expiry;

    std::vector<uint8_t> payloadBytes(payload.begin(), payload.end());
    std::vector<uint8_t> signedMessage(crypto_sign_BYTES + payloadBytes.size());
    unsigned long long signedLen = 0;

    crypto_sign(signedMessage.data(), &signedLen,
                payloadBytes.data(), payloadBytes.size(),
                privateKey);

    std::string raw = base32Encode(signedMessage);
    return formatSerial(raw);
}

int main(int argc, char* argv[])
{
    if (sodium_init() < 0)
    {
        std::cerr << "Failed to initialise libsodium\n";
        return 1;
    }

    if (argc < 2)
    {
        std::cout << "Usage:\n"
                  << "  ./keygen --generate-keys\n"
                  << "  ./keygen --name \"Customer Name\" --tier pro --expiry 2027-12-31\n";
        return 1;
    }

    std::string arg1 = argv[1];

    if (arg1 == "--generate-keys")
    {
        generateKeys();
        return 0;
    }

    if (arg1 == "--name")
    {
        std::string name, tier = "standard", expiry = "9999-12-31";

        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];
            if (a == "--name"   && i + 1 < argc) name   = argv[++i];
            if (a == "--tier"   && i + 1 < argc) tier   = argv[++i];
            if (a == "--expiry" && i + 1 < argc) expiry = argv[++i];
        }

        if (name.empty())
        {
            std::cerr << "Error: --name is required\n";
            return 1;
        }

        std::string serial = generateSerial(name, tier, expiry);
        if (serial.empty()) return 1;

        std::cout << "\nCustomer : " << name << "\n";
        std::cout << "Tier     : " << tier << "\n";
        std::cout << "Expiry   : " << expiry << "\n";
        std::cout << "Serial   : " << serial << "\n\n";

        return 0;
    }

    std::cerr << "Unknown argument: " << arg1 << "\n";
    return 1;
}
