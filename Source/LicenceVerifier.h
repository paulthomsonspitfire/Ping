// =============================================================================
// LicenceVerifier.h  —  Drop this into your JUCE plugin project
// =============================================================================
//
// DEPENDENCIES:
//   libsodium  (https://libsodium.org)
//
//   In your CMakeLists.txt or Projucer, link against libsodium:
//     target_link_libraries(YourPlugin PRIVATE sodium)
//
//   Or with Projucer: add libsodium to "Extra Libraries" and add the
//   include path for sodium.h to "Header Search Paths".
//
// SETUP:
//   1. Run your keygen with --generate-keys
//   2. Copy the PUBLIC_KEY array it prints into the space below
//   3. Delete private_key.bin from anywhere near your plugin project
//
// USAGE in your plugin:
//   LicenceVerifier verifier;
//   auto result = verifier.activate("Paul Hartnoll", "ABCDE-FGHIJ-...");
//   if (result.valid) {
//       // licensed! result.tier, result.expiry, result.normalisedName available
//   }
// =============================================================================

#pragma once

#include <sodium.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>

// =============================================================================
// *** PASTE YOUR PUBLIC KEY HERE ***
// Run ./keygen --generate-keys and copy the array it prints.
// This is safe to ship inside your plugin binary.
// =============================================================================
static const uint8_t PUBLIC_KEY[32] = {
    0xDE, 0x35, 0x42, 0x4D, 0xED, 0x83, 0x18, 0x06,
    0xCA, 0x83, 0x40, 0xF5, 0x4C, 0xF8, 0xA1, 0x9B,
    0xDC, 0xF3, 0xDD, 0xD7, 0x06, 0xCD, 0x04, 0x4C,
    0x95, 0x1E, 0xC9, 0x26, 0x1F, 0xC2, 0xE4, 0x6D
};


// =============================================================================
// LicenceResult  —  what you get back from activate()
// =============================================================================
struct LicenceResult
{
    bool        valid          = false;
    bool        expired        = false;
    std::string normalisedName;   // the name as it was encoded in the serial
    std::string tier;             // "demo", "standard", or "pro"
    std::string expiry;           // "YYYY-MM-DD" or "9999-12-31" for perpetual
    std::string errorMessage;
};


// =============================================================================
// LicenceVerifier
// =============================================================================
class LicenceVerifier
{
public:

    LicenceVerifier()
    {
        sodium_init(); // safe to call multiple times
    }

    // Main entry point.
    // Pass in exactly what the user typed for their name and serial.
    // The verifier normalises internally so capitalisation doesn't matter.
    LicenceResult activate(const std::string& enteredName,
                           const std::string& enteredSerial)
    {
        LicenceResult result;

        // 1. Decode the serial from base32 back to bytes
        std::string cleanSerial = stripFormatting(enteredSerial);
        std::vector<uint8_t> signedMessage = base32Decode(cleanSerial);

        if (signedMessage.size() <= crypto_sign_BYTES)
        {
            result.errorMessage = "Serial is too short to be valid.";
            return result;
        }

        // 2. Verify the signature and extract the payload
        std::vector<uint8_t> payload(signedMessage.size() - crypto_sign_BYTES);
        unsigned long long payloadLen = 0;

        int verifyResult = crypto_sign_open(
            payload.data(), &payloadLen,
            signedMessage.data(), signedMessage.size(),
            PUBLIC_KEY
        );

        if (verifyResult != 0)
        {
            result.errorMessage = "Serial number is not valid.";
            return result;
        }

        payload.resize(payloadLen);

        // 3. Parse the payload  (format: "normalisedname|tier|expiry")
        std::string payloadStr(payload.begin(), payload.end());
        auto parts = splitString(payloadStr, '|');

        if (parts.size() != 3)
        {
            result.errorMessage = "Serial format is unrecognised.";
            return result;
        }

        std::string encodedName   = parts[0];
        std::string encodedTier   = parts[1];
        std::string encodedExpiry = parts[2];

        // 4. Check the entered name matches what's encoded in the serial
        std::string normalisedInput = normaliseName(enteredName);

        if (normalisedInput != encodedName)
        {
            result.errorMessage = "The name entered does not match this serial number.";
            return result;
        }

        // 5. Check expiry
        bool expired = isExpired(encodedExpiry);

        // 6. All good — populate result
        result.valid          = true;
        result.expired        = expired;
        result.normalisedName = encodedName;
        result.tier           = encodedTier;
        result.expiry         = encodedExpiry;

        if (expired)
            result.errorMessage = "This licence expired on " + encodedExpiry + ".";

        return result;
    }

    // Returns a display string suitable for showing in your About screen,
    // e.g.  "Licensed to paul hartnoll  |  pro"
    // Call this after a successful activate() to show in your UI.
    static std::string licenceDisplayString(const LicenceResult& r)
    {
        if (!r.valid) return "Unlicensed";
        std::string s = "Licensed to " + r.normalisedName + "  |  " + r.tier;
        if (r.expiry != "9999-12-31")
            s += "  |  expires " + r.expiry;
        return s;
    }


private:

    // -------------------------------------------------------------------------
    // Name normalisation — must exactly match the keygen's normaliseName()
    // -------------------------------------------------------------------------
    static std::string normaliseName(const std::string& name)
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

    // -------------------------------------------------------------------------
    // Base32 decode — strips hyphens and spaces before decoding
    // -------------------------------------------------------------------------
    static std::string stripFormatting(const std::string& serial)
    {
        std::string result;
        for (char c : serial)
            if (c != '-' && c != ' ')
                result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return result;
    }

    static const char* base32Alphabet()
    {
        return "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    }

    static std::vector<uint8_t> base32Decode(const std::string& encoded)
    {
        std::vector<uint8_t> result;
        int buffer = 0;
        int bitsLeft = 0;

        for (char c : encoded)
        {
            const char* pos = std::strchr(base32Alphabet(), c);
            if (!pos) continue;

            buffer <<= 5;
            buffer |= static_cast<int>(pos - base32Alphabet());
            bitsLeft += 5;

            if (bitsLeft >= 8)
            {
                bitsLeft -= 8;
                result.push_back(static_cast<uint8_t>((buffer >> bitsLeft) & 0xFF));
            }
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // String splitting
    // -------------------------------------------------------------------------
    static std::vector<std::string> splitString(const std::string& s, char delimiter)
    {
        std::vector<std::string> parts;
        std::stringstream ss(s);
        std::string part;
        while (std::getline(ss, part, delimiter))
            parts.push_back(part);
        return parts;
    }

    // -------------------------------------------------------------------------
    // Expiry check  —  compares YYYY-MM-DD strings against today's date.
    // String comparison works correctly for ISO date format.
    // -------------------------------------------------------------------------
    static bool isExpired(const std::string& expiryDate)
    {
        if (expiryDate == "9999-12-31") return false;

        std::time_t now = std::time(nullptr);
        std::tm* t = std::gmtime(&now);

        char todayBuf[11];
        std::snprintf(todayBuf, sizeof(todayBuf), "%04d-%02d-%02d",
                      t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);

        std::string today(todayBuf);
        return today > expiryDate;
    }
};
